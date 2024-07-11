// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Emulation of Kinetoscope video streaming hardware in BlastEm.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <curl/curl.h>
#include <pthread.h>

#include "genesis.h"
#include "util.h"

#include "segavideo_format.h"

#if defined(_WIN32)
// Windows header for ntohs and ntohl.
# include <winsock2.h>
#else
// Linux headers for ntohs and ntohl.
# include <arpa/inet.h>
# include <netinet/in.h>
#endif

#define CMD_ECHO        0x00
#define CMD_LIST_VIDEOS 0x01
#define CMD_START_VIDEO 0x02
#define CMD_STOP_VIDEO  0x03
#define CMD_FLIP_REGION 0x04
#define CMD_GET_ERROR   0x05

// NOTE: The addresses sent to us are all relative to the base of 0xA13000.
// So we only check the offset from there.  All addresses are even because the
// cartridge interface does not have a wire for A0.  So all port accesses are
// 16-bit aligned.
#define KINETOSCOPE_PORT_COMMAND  0x00  // command, only the low 8 bits are read
#define KINETOSCOPE_PORT_ARG      0x02  // arg, only the low 8 bits are read
#define KINETOSCOPE_PORT_UNUSED_2 0x04  // register exists in hardware, unused
#define KINETOSCOPE_PORT_UNUSED_3 0x06  // register exists in hardware, unused
#define KINETOSCOPE_PORT_TOKEN    0x08  // low 1 bit, set on write by Sega
#define KINETOSCOPE_PORT_ERROR    0x0A  // low 1 bit, clear on write by Sega

#define TOKEN_CONTROL_TO_SEGA     0
#define TOKEN_CONTROL_TO_STREAMER 1

#define DELAY_GENERAL 0.5
#define DELAY_NETWORK 5

// SRAM regions.
#define REGION_OFFSET_MASK 0x100000  // 0MB or 1MB offset
#define REGION_SIZE        0x100000  // 1MB size

// Offset of paddingBytes field from the end of the headers
#define PADDING_BYTES_OFFSET_FROM_END_OF_HEADER \
  (sizeof(SegaVideoChunkHeader) - offsetof(SegaVideoChunkHeader, paddingBytes))

#define VIDEO_BASE_URL "http://storage.googleapis.com/sega-kinetoscope/canned-videos/"
#define VIDEO_CATALOG_URL VIDEO_BASE_URL "catalog.bin"

static uint8_t* global_sram_buffer = NULL;
static uint32_t global_sram_size = 0;
static uint16_t global_command = 0;
static uint16_t global_token = TOKEN_CONTROL_TO_SEGA;
static uint16_t global_error = 0;
static char* global_error_str = NULL;
static uint16_t global_arg;
static uint32_t global_ready_cycle = (uint32_t)-1;
static char* global_video_url = NULL;
static uint32_t global_chunk_size = 0;
static uint32_t global_chunk_num = 0;
static uint32_t global_chunks_left = 0;
static pthread_t global_fetch_thread;
static volatile bool global_fetch_busy = false;

// Where we read from next:
static uint32_t global_video_url_start_byte;
// Where we write to next:
static uint32_t global_sram_offset;

static uint32_t cycle_delay(void *context, double delay_seconds) {
  m68k_context *m68k = (m68k_context *)context;
  genesis_context *genesis = (genesis_context *)m68k->system;
  return delay_seconds * genesis->master_clock;
}

static void write_sram(uint32_t offset, const uint8_t* data, uint32_t size) {
  if (offset + size > global_sram_size) {
    warning("Kinetoscope: tried to overflow SRAM!"
            " (offset: 0x%08x, size: 0x%08x)\n",
            offset, size);
    return;
  }

  for (size_t i = 0; i < size; i++) {
    // XOR with 1 swaps every 2 bytes in the output.
    // TODO: WHY?  This shouldn't be necessary!  Is this a bug in the emulator?
    // An assumption about what kind of data is in a direct-access buffer?
    // Should this nonsense only happen for little-endian builds?
    global_sram_buffer[(offset + i) ^ 1] = data[i];
  }
}

static void report_error(const char *message) {
  if (global_error == 0) {
    printf("Kinetoscope: Simulating error: %s\n", message);
    global_error = 1;
    if (global_error_str) {
      free(global_error_str);
    }
    global_error_str = strdup(message);
  } else {
    printf("Kinetoscope: Ignoring error: %s\n", message);
  }
}

static void write_error_to_sram() {
  write_sram(0, (const uint8_t*)global_error_str, strlen(global_error_str));
}

// Writes HTTP data to SRAM.
static size_t http_data_to_sram(char* data, size_t size, size_t n, void* ctx) {
  write_sram(global_sram_offset, (const uint8_t*)data, size * n);
  global_sram_offset += size * n;
  return size * n;
}

typedef struct HttpBuffer {
  char* data;
  size_t offset;
  size_t max;
} HttpBuffer;

// Writes HTTP data to a fixed-size buffer.
static size_t http_data_to_buffer(char* data, size_t size, size_t n, void* ctx) {
  HttpBuffer* buffer = (HttpBuffer*)ctx;
  size_t to_write = size * n;

  if (buffer->offset + to_write > buffer->max) {
    to_write = buffer->max - buffer->offset;
  }

  if (to_write) {
    memcpy(buffer->data + buffer->offset, data, to_write);
    buffer->offset += to_write;
  }

  return size * n;
}

typedef size_t (*WriteCallback)(char*, size_t, size_t, void *);

static bool fetch_range(const char* url, size_t first_byte, size_t size,
                        WriteCallback write_callback, void* ctx) {
  char range[32];
  size_t last_byte = first_byte + size - 1;
  snprintf(range, 32, "%zd-%zd", first_byte, last_byte);
  // snprintf doesn't guarantee a terminator when it overflows.
  range[31] = '\0';

  CURL* handle = curl_easy_init();
  curl_easy_setopt(handle, CURLOPT_URL, url);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, ctx);
  if (first_byte != 0 || last_byte != -1) {
    curl_easy_setopt(handle, CURLOPT_RANGE, range);
  }

  CURLcode res = curl_easy_perform(handle);
  long http_code = 0;
  curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &http_code);
  curl_easy_cleanup(handle);

  printf("Kinetoscope: url = %s, CURLcode = %d, http status = %ld\n",
         url, res, http_code);
  if (res != CURLE_OK) {
    char buf[64];
    snprintf(buf, 64, "Curl error: %s", curl_easy_strerror(res));
    report_error(buf);
  }

  return res == CURLE_OK && (http_code == 200 || http_code == 206);
}

static bool fetch_range_to_sram(const char* url, size_t first_byte,
                                size_t size) {
  return fetch_range(url, first_byte, size, http_data_to_sram, NULL);
}

static bool fetch_to_sram(const char* url) {
  return fetch_range_to_sram(url, 0, -1);
}

static bool fetch_range_to_buffer(const char* url, void* data,
                                  size_t first_byte, size_t size) {
  HttpBuffer buffer;
  buffer.data = (char*)data;
  buffer.offset = 0;
  buffer.max = size;
  return fetch_range(url, first_byte, size, http_data_to_buffer, &buffer);
}

static bool fetch_to_buffer(const char* url, void* data, size_t size) {
  return fetch_range_to_buffer(url, data, 0, size);
}

static void stop_video() {
  // TODO: interrupt curl transfers
}

static void* fetch_thread(void* ignored_arg) {
  while (true) {
    while (!global_fetch_busy) {
      usleep(10 * 1000);  // 10ms
    }

    size_t size = global_chunk_size;
    if (global_chunk_num == 0) {
      size += sizeof(SegaVideoHeader);
    }

    if (!fetch_range_to_sram(global_video_url, global_video_url_start_byte,
                             size)) {
      char buf[64];
      snprintf(buf, 64, "Failed to fetch video! (chunk %d)", global_chunk_num);
      report_error(buf);
    } else {
      global_chunk_num++;
      global_chunks_left--;
      global_video_url_start_byte += size;
      global_sram_offset = (global_chunk_num & 1) ? REGION_SIZE : 0;
    }

    global_fetch_busy = false;
  }

  return NULL;
}

static void fetch_chunk() {
  // Check for underflow
  if (global_fetch_busy) {
    report_error("Underflow detected! Internet too slow?");
    return;
  }

  // Flag the thread to start the next chunk.
  global_fetch_busy = true;
}

static void wait_for_chunk() {
  while (global_fetch_busy) {
    usleep(10 * 1000);  // 10ms
  }
}

static void start_video() {
  // Look up the video URL.
  uint16_t video_index = global_arg;
  if (video_index > 127) {
    char buf[64];
    snprintf(buf, 64, "Invalid video index requested! (%d)", (int)video_index);
    report_error(buf);
    return;
  }

  SegaVideoHeader header;
  if (!fetch_range_to_buffer(VIDEO_CATALOG_URL, &header,
                             sizeof(header) * video_index, sizeof(header))) {
    char buf[64];
    snprintf(buf, 64, "Failed to fetch catalog index! (%d)", (int)video_index);
    report_error(buf);
    return;
  }

  // header.relative_url should be nul-terminated, but just in case, use
  // strnlen and fail if we get the maximum size, which would indicate no
  // nul-terminator.
  const char* path = header.relative_url;
  size_t path_len = strnlen(path, sizeof(header.relative_url));
  if (path_len == sizeof(header.relative_url)) {
    char buf[64];
    snprintf(buf, 64, "Invalid catalog data at index! (%d)", (int)video_index);
    report_error(buf);
    return;
  }

  size_t base_len = strlen(VIDEO_BASE_URL);
  if (global_video_url) {
    free(global_video_url);
  }
  global_video_url = (char*)malloc(base_len + path_len + 1);
  memcpy(global_video_url, VIDEO_BASE_URL, base_len);
  memcpy(global_video_url + base_len, path, path_len);
  global_video_url[base_len + path_len] = '\0';

  if (!fetch_to_buffer(global_video_url, &header, sizeof(header))) {
    report_error("Failed to fetch video! (header)");
    return;
  }

  global_chunk_size = ntohl(header.chunkSize);
  global_chunks_left = ntohl(header.totalChunks);
  global_chunk_num = 0;
  global_sram_offset = 0;
  global_video_url_start_byte = 0;

  // Fill the first region.
  fetch_chunk();
  wait_for_chunk();

  if (global_chunks_left) {
    // Fill the second region as well.
    fetch_chunk();
    wait_for_chunk();
  }
}

static void flip_region() {
  if (!global_chunks_left) {
    return;
  }

  fetch_chunk();
}

static void get_video_list() {
  printf("Kinetoscope: list\n");

  global_sram_offset = 0;
  if (!fetch_to_sram(VIDEO_CATALOG_URL)) {
    report_error("Failed to download video catalog!");
    return;
  }
}

static void execute_command() {
  if (global_command == CMD_ECHO) {
    // Used by the ROM to check for the necessary streaming hardware.
    uint16_t value = global_arg;
    printf("Kinetoscope: CMD_ECHO 0x%04x\n", value);
    write_sram(0, (const uint8_t*)&value, sizeof(value));
  } else if (global_command == CMD_LIST_VIDEOS) {
    printf("Kinetoscope: CMD_LIST_VIDEOS\n");
    get_video_list();
  } else if (global_command == CMD_START_VIDEO) {
    printf("Kinetoscope: CMD_START_VIDEO\n");
    start_video();
  } else if (global_command == CMD_STOP_VIDEO) {
    printf("Kinetoscope: CMD_STOP_VIDEO\n");
    stop_video();
  } else if (global_command == CMD_FLIP_REGION) {
    printf("Kinetoscope: CMD_FLIP_REGION\n");
    flip_region();
  } else if (global_command == CMD_GET_ERROR) {
    printf("Kinetoscope: CMD_GET_ERROR\n");
    write_error_to_sram();
  } else {
    printf("Kinetoscope: unknown command 0x%02x\n", global_command);
  }

  // Completed.
  printf("Kinetoscope: command complete.\n");
  global_token = TOKEN_CONTROL_TO_SEGA;
}

void kinetoscope_init(void *sram_buffer, uint32_t sram_size) {
  global_sram_buffer = sram_buffer;
  global_sram_size = sram_size;

  curl_global_init(CURL_GLOBAL_ALL);

  global_fetch_busy = false;
  pthread_create(&global_fetch_thread, NULL, fetch_thread, NULL);

#if 0
  // To test error handling, simulate no connection
  report_error("Wired connection failed and WiFi not configured!  Have you tried connecting the thing to the other thing?");
#endif
}

void *kinetoscope_write_16(uint32_t address, void *context, uint16_t value) {
  m68k_context *m68k = (m68k_context *)context;

  if (address == KINETOSCOPE_PORT_COMMAND) {
    global_command = value;
  } else if (address == KINETOSCOPE_PORT_ARG) {
    global_arg = value;
  } else if (address == KINETOSCOPE_PORT_TOKEN) {
    // This bit is always set on write by Sega, no matter the value.
    global_token = TOKEN_CONTROL_TO_STREAMER;
    printf("Kinetoscope: Received command 0x%02x\n", global_command);
    // Decide when the command will execute, simulating async operation of
    // the cart's secondary processor.
    double delay_seconds =
        global_command == CMD_LIST_VIDEOS ? DELAY_NETWORK : DELAY_GENERAL;
    global_ready_cycle =
        m68k->current_cycle + cycle_delay(context, delay_seconds);
  } else if (address == KINETOSCOPE_PORT_ERROR) {
    printf("Kinetoscope: Clearing error bit\n");
    // This bit is always cleared on write by Sega, no matter the value.
    global_error = 0;  // always cleared on write by Sega
  } else {
    printf("Kinetoscope: Unknown address 0x%02x\n", address);
  }
  return context;
}

void *kinetoscope_write_8(uint32_t address, void *context, uint8_t value) {
  uint32_t aligned_address = address & ~1;
  if (address != aligned_address) {
    // second byte = low byte in big-endian systems
    // Our control ports don't use this.
    return context;
  } else {
    // first byte = high byte in big-endian systems
    return kinetoscope_write_16(address, context, ((uint16_t)value) << 8);
  }
}

uint16_t kinetoscope_read_16(uint32_t address, void *context) {
  m68k_context *m68k = (m68k_context *)context;

  if (m68k->current_cycle >= global_ready_cycle &&
      global_token == TOKEN_CONTROL_TO_STREAMER) {
    execute_command();
  }

  if (address == KINETOSCOPE_PORT_TOKEN) {
    // These are always 1-bit values.
    //printf("Kinetoscope: Reading token bit: %d\n", global_token);
    return global_token ? 1 : 0;
  } else if (address == KINETOSCOPE_PORT_ERROR) {
    //printf("Kinetoscope: Reading error bit: %d\n", global_error);
    // These are always 1-bit values.
    return global_error ? 1 : 0;
  } else {
    // Register values are write-only from the Sega.
    return 0;
  }
}

uint8_t kinetoscope_read_8(uint32_t address, void *context) {
  uint32_t aligned_address = address & ~1;
  if (address != aligned_address) {
    // second byte = low byte in big-endian systems
    return kinetoscope_read_16(aligned_address, context) & 0xff;
  } else {
    // first byte = high byte in big-endian systems
    return kinetoscope_read_16(aligned_address, context) >> 8;
  }
}
