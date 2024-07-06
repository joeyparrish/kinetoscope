// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Emulation of Kinetoscope video streaming hardware in BlastEm.

#include <stdint.h>
#include <string.h>

#include <curl/curl.h>

#include "genesis.h"
#include "util.h"

#include "segavideo_format.h"
#include "segavideo_parser.h"

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
static uint8_t* global_video_data = NULL;

// Where we read from next:
static uint32_t global_video_data_offset;
// Where we write to next:
static uint32_t global_sram_offset;
// Size of the last chunk written:
static uint32_t global_last_chunk_size;

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
  printf("Kinetoscope: Simulating error: %s\n", message);
  global_error = 1;
  if (global_error_str) {
    free(global_error_str);
  }
  global_error_str = strdup(message);
}

static void write_error_to_sram() {
  write_sram(0, (const uint8_t*)global_error_str, strlen(global_error_str));
}

// Writes HTTP data to SRAM.
static size_t http_data_callback(void *data, size_t size, size_t n, void *ctx) {
  write_sram(global_sram_offset, data, size * n);
  global_sram_offset += size * n;
  return size * n;
}

static bool fetch_range_to_sram(const char* url, int first_byte, int last_byte) {
  char range[32];
  snprintf(range, 32, "%d-%d", first_byte, last_byte);
  // snprintf doesn't guarantee a terminator when it overflows.
  range[31] = '\0';

  global_sram_offset = 0;

  CURL* handle = curl_easy_init();
  curl_easy_setopt(handle, CURLOPT_URL, url);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, http_data_callback);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, NULL);
  if (first_byte != 0 || last_byte != -1) {
    curl_easy_setopt(handle, CURLOPT_RANGE, range);
  }

  CURLcode res = curl_easy_perform(handle);
  long http_code = 0;
  curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &http_code);
  curl_easy_cleanup(handle);

  return res == CURLE_OK && (http_code == 200 || http_code == 206);
}

static bool fetch_to_sram(const char* url) {
  return fetch_range_to_sram(url, 0, -1);
}

#if 0 // FIXME: Reimplement video chunk logic by writing from libcurl to SRAM
static void write_sram_video_chunk(const SegaVideoChunkInfo* chunk) {
  uint32_t chunk_size = (uint32_t)(chunk->end - chunk->start);
  uint32_t header_size = sizeof(SegaVideoChunkHeader) +
      ((global_video_data_offset == 0) ? sizeof(SegaVideoHeader) : 0);

  printf("Kinetoscope: Writing chunk size 0x%x from input offset 0x%08x"
         " to output offset 0x%08x\n",
         chunk_size, global_video_data_offset, global_sram_offset);

  // First write the header.
  write_sram(global_sram_offset,
             global_video_data + global_video_data_offset,
             header_size);
  global_video_data_offset += header_size;
  global_sram_offset += header_size;

  // Now get the number of padding bytes from the input stream.
  uint16_t* pointer_to_padding_bytes = (uint16_t*)(
      global_video_data + global_video_data_offset
      - PADDING_BYTES_OFFSET_FROM_END_OF_HEADER);
  uint16_t padding_bytes = ntohs(*pointer_to_padding_bytes);
  printf("Kinetoscope: original padding bytes = %d\n", (int)padding_bytes);

  // Compute the actual data size and move the input offset past the padding.
  uint32_t data_size = chunk_size - header_size - padding_bytes;
  global_video_data_offset += padding_bytes;

  // Adjust the padding to align audio in the output stream.
  uint32_t padding_remainder = global_sram_offset % 256;
  padding_bytes = padding_remainder ? (256 - padding_remainder) : 0;
  printf("Kinetoscope: SRAM offset after header: 0x%08x\n", global_sram_offset);
  printf("Kinetoscope: new padding bytes = %d\n", (int)padding_bytes);
  uint16_t network_order_padding_bytes = htons(padding_bytes);
  write_sram(global_sram_offset - PADDING_BYTES_OFFSET_FROM_END_OF_HEADER,
             (const uint8_t*)&network_order_padding_bytes,
             sizeof(network_order_padding_bytes));

  // Skip the padding in the output stream.
  global_sram_offset += padding_bytes;

  // Write the data portion.
  write_sram(global_sram_offset,
             global_video_data + global_video_data_offset,
             data_size);
  global_video_data_offset += data_size;
  global_sram_offset += data_size;

  // Store the last chunk size.
  global_last_chunk_size = chunk_size;
}
#endif

static void stop_video() {
  if (global_video_data) {
    free(global_video_data);
  }
  global_video_data = NULL;
}

static void fill_region() {
#if 0 // FIXME: Reimplement video chunk logic by writing from libcurl to SRAM
  uint32_t start_region = global_sram_offset & REGION_OFFSET_MASK;

  while (true) {
    uint32_t start_offset = global_sram_offset;
    uint32_t end_offset = start_offset + global_last_chunk_size - 1;
    uint32_t end_region = end_offset & REGION_OFFSET_MASK;

    if (end_region == start_region) {
      // Same region, go ahead and write it.
      SegaVideoChunkInfo chunk;
      segavideo_parseChunk(global_video_data + global_video_data_offset,
                           &chunk);
      // Advances global_sram_offset and global_video_data_offset:
      write_sram_video_chunk(&chunk);
      if (!chunk.numFrames || !chunk.audioSamples) {
        // No more chunks!
        printf("Kinetoscope: No more chunks!"
               " Final header written to offset 0x%08x\n",
               (int)(global_sram_offset - sizeof(SegaVideoChunkHeader)));
        stop_video();
        return;
      }
    } else {
      // The next chunk would wrap into a new region.
      // This region is now full.
      // Start the next write in the alternate region.
      global_sram_offset = start_region ^ REGION_SIZE;
      printf("Kinetoscope: Start region 0x%08x is full, next region 0x%08x\n",
             start_region, global_sram_offset);
      return;
    }
  }
#endif
}

static void start_video() {
  char* video_path = NULL;

  if (!video_path) {
    // Write a 0 to SRAM, which will make sure any old data doesn't look like a
    // valid video.
    write_sram(0, (const uint8_t*)"", 1);
    printf("Kinetoscope: Unknown video (%d)!\n", global_arg);
    return;
  }

#if 0 // FIXME: Reimplement video chunk logic by writing from libcurl to SRAM
  if (global_video_data) {
    free(global_video_data);
  }
  global_video_data = (uint8_t*)malloc(size);
  if (!global_video_data) {
    printf("Kinetoscope: Failed to allocate space for video (%s)!\n",
        video_path);
    free(video_path);
    return;
  }

  size_t bytes_read = fread(global_video_data, 1, size, f);
  if (bytes_read != size) {
    printf("Kinetoscope: Failed to read video (%s; retval=%d)!\n",
        video_path, (int)bytes_read);
    free(video_path);
    free(global_video_data);
    global_video_data = NULL;
    return;
  }

  printf("Kinetoscope: Loaded video (%s).\n", video_path);
  fclose(f);
  free(video_path);

  if (!segavideo_validateHeader(global_video_data)) {
    // Error printed inside the function on failure.
    free(global_video_data);
    global_video_data = NULL;
    return;
  }

  global_video_data_offset = 0;
  global_sram_offset = 0;

  const uint8_t* chunk_start = global_video_data + sizeof(SegaVideoHeader);
  SegaVideoChunkInfo chunk;
  segavideo_parseChunk(chunk_start, &chunk);
  // Make the first chunk start at the overall video header, not just the chunk
  // header that follows.
  chunk.start = global_video_data;

  uint32_t chunk_size = (uint32_t)(chunk.end - chunk.start);
  uint32_t start_offset = global_sram_offset;
  uint32_t end_offset = start_offset + chunk_size - 1;
  uint32_t start_region = start_offset & REGION_OFFSET_MASK;
  uint32_t end_region = end_offset & REGION_OFFSET_MASK;

  if (start_region != end_region) {
    printf("Kinetoscope: First video chunk overflows the region!\n");
    stop_video();
    return;
  }

  // Advances global_sram_offset and global_video_data_offset:
  write_sram_video_chunk(&chunk);

  // Fill the first region with additional chunks if possible.
  // In case of EOF, this frees global_video_data through stop_video().
  fill_region();

  if (global_video_data) {
    // Fill the second region as well.
    fill_region();
  }
#endif
}

static void flip_region() {
  if (!global_video_data) {
    printf("Kinetoscope: FLIP_REGION command while not playing!\n");
    return;
  }

  fill_region();
}

static void get_video_list() {
  printf("Kinetoscope: list\n");

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
