// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Emulation of Kinetoscope video streaming hardware.
// An emscripten build requires -s FETCH=1 at link time.

#if defined(_WIN32)
// Enable Windows 10 APIs.  Must be defined before any headers are included.
# define _WIN32_WINNT 0x0A00
#endif

#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kinetoscope/software/player/inc/segavideo_format.h"
#include "kinetoscope/common/video-server.h"
#include "kinetoscope/emulator-patches/fetch.c"

#if defined(_WIN32)
// Windows header for ntohs and ntohl.
# include <winsock2.h>
// For GetTickCount64.
# include <windows.h>
#else
// Linux headers for ntohs and ntohl.
# include <arpa/inet.h>
# include <netinet/in.h>
// For clock_gettime.
# include <time.h>
#endif

#define CMD_ECHO        0x00
#define CMD_LIST_VIDEOS 0x01
#define CMD_START_VIDEO 0x02
#define CMD_STOP_VIDEO  0x03
#define CMD_FLIP_REGION 0x04
#define CMD_GET_ERROR   0x05
#define CMD_CONNECT_NET 0x06
#define CMD_MARCH_TEST  0x07

// NOTE: The addresses sent to us are all relative to the base of 0xA13000.
// So we only check the offset from there.  All addresses are even because the
// cartridge interface does not have a wire for A0.  So all port accesses are
// 16-bit aligned.
#define KINETOSCOPE_PORT_COMMAND  0x10  // command, only the low 8 bits are read
#define KINETOSCOPE_PORT_ARG      0x12  // arg, only the low 8 bits are read
#define KINETOSCOPE_PORT_TOKEN    0x08  // low 1 bit, set on write by Sega
#define KINETOSCOPE_PORT_ERROR    0x0A  // low 1 bit, clear on write by Sega

#define TOKEN_CONTROL_TO_SEGA     0
#define TOKEN_CONTROL_TO_STREAMER 1

#define SIMULATED_PROCESSING_DELAY 100  // milliseconds

// SRAM regions.
#define SRAM_BANK_0_OFFSET 0
#define SRAM_BANK_1_OFFSET (1 << 20)  // 1MB
#define SRAM_SIZE          (2 << 20)  // 2MB

static void write_sram(const uint8_t* data, uint32_t size);
static void reset_sram(int bank);

// Macros to complete sram_march_test in sram-common.h
#define SRAM_MARCH_TEST_START(bank) reset_sram(bank)
#define SRAM_MARCH_TEST_DATA(offset, data) write_sram(&data, 1)
#define SRAM_MARCH_TEST_END() {}

// Defines sram_march_test()
#include "kinetoscope/common/sram-common.h"

// Macros for rle-common.h
#define SRAM_WRITE(buffer, size) write_sram(buffer, size)

// Defines rle_to_sram()
#include "kinetoscope/common/rle-common.h"

typedef struct kinetoscope_emulation_context_t {
  // SRAM
  // =========
  // backing store for emulated SRAM banks
  uint8_t* sram_buffer;
  // position we write to next
  uint32_t sram_offset;

  // Communication
  // =============
  // command port from Sega to uC
  uint16_t command;
  // argument port (related to command) from Sega to uC
  uint16_t arg;
  // control token, 1 bit, set by Sega, cleared by uC
  uint16_t token;
  // error token, 1 bit, set by uC, cleared by Sega
  uint16_t error;
  // a stored error string to deliver later when requested
  char* error_str;
  // time when the current command will be complete, to emulate a processing delay
  uint64_t ready_time;
  // commands may be async, so this flag tracks a command in process internally
  bool command_busy;

  // Streaming
  // =========
  // URL of the current video
  char* video_url;
  // consistent size of uncompressed video chunks
  uint32_t chunk_size;
  // the number of the next chunk to fetch
  uint32_t chunk_num;
  // how many chunks are left
  uint32_t chunks_left;
  // postition we read from next
  uint32_t video_url_start_byte;
  // whether the content is compressed or not
  bool compressed;
  // the header of the video we're starting
  SegaVideoHeader header;
  // the index of chunk offsets for compressed video
  SegaVideoIndex index;

  // Threading
  // =========
  // whether the thread is busy doing something right now
  volatile bool fetch_busy;
} kinetoscope_emulation_context_t;

static kinetoscope_emulation_context_t kinetoscope = { .sram_buffer = NULL };

static void* fetch_thread(void* ignored_arg);

void* kinetoscope_init() {
#if !defined(__EMSCRIPTEN__)
  curl_global_init(CURL_GLOBAL_ALL);
#endif

  if (!kinetoscope.sram_buffer) {
    kinetoscope.sram_buffer = malloc(SRAM_SIZE);
  }
  kinetoscope.token = TOKEN_CONTROL_TO_SEGA;
  kinetoscope.error = 0;
  kinetoscope.error_str = NULL;
  kinetoscope.ready_time = (uint64_t)-1;
  kinetoscope.command_busy = false;
  kinetoscope.video_url = NULL;
  kinetoscope.fetch_busy = false;

#if 0
  // To test error handling, simulate no connection
  report_error("Wired connection failed and WiFi not configured!  Have you tried connecting the thing to the other thing?");
#endif

  // Give the emulator the address of the SRAM buffer.
  return kinetoscope.sram_buffer;
}

// Current time in milliseconds.
static uint64_t ms_now() {
#if defined(_WIN32)
  return GetTickCount64();
#else
  struct timespec tp;
  int rv = clock_gettime(CLOCK_MONOTONIC, &tp);
  if (rv != 0) {
    rv = clock_gettime(CLOCK_REALTIME, &tp);
  }
  if (rv != 0) {
    fprintf(stderr, "Kinetoscope: failed to get clock!\n");
    return (uint64_t)-1;
  }
  return (tp.tv_sec * 1000) + (tp.tv_nsec / 1e6);
#endif
}

static void reset_sram(int bank) {
  kinetoscope.sram_offset = bank ? SRAM_BANK_1_OFFSET : SRAM_BANK_0_OFFSET;
}

static void write_sram(const uint8_t* data, uint32_t size) {
  if (kinetoscope.sram_offset + size > SRAM_SIZE) {
    fprintf(stderr, "Kinetoscope: tried to overflow SRAM!"
            " (offset: 0x%08x, size: 0x%08x)\n",
            kinetoscope.sram_offset, size);
    return;
  }

  for (size_t i = 0; i < size; i++) {
    // XOR with 1 swaps every 2 bytes in the output.
    // TODO: WHY?  This shouldn't be necessary!  Is this a bug in the emulator?
    // An assumption about what kind of data is in a direct-access buffer?
    // Should this nonsense only happen for little-endian builds?
    kinetoscope.sram_buffer[kinetoscope.sram_offset ^ 1] = data[i];
    kinetoscope.sram_offset++;
  }
}

static void report_error(const char *format, ...) {
  char message[256];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);

  if (kinetoscope.error == 0) {
    printf("Kinetoscope: Simulating error: %s\n", message);
    kinetoscope.error = 1;
    if (kinetoscope.error_str) {
      free(kinetoscope.error_str);
    }
    kinetoscope.error_str = strdup(message);
  } else {
    printf("Kinetoscope: Ignoring error: %s\n", message);
  }
}

static void write_error_to_sram() {
  reset_sram(0);
  write_sram((const uint8_t*)kinetoscope.error_str, strlen(kinetoscope.error_str));
}

// Writes HTTP data to SRAM.
static size_t http_data_to_sram(char* data, size_t size, size_t n, void* ctx) {
  if (kinetoscope.compressed) {
    rle_to_sram((const uint8_t*)data, size * n);
  } else {
    write_sram((const uint8_t*)data, size * n);
  }
  return size * n;
}

typedef struct HttpBuffer {
  char* data;
  size_t offset;
  size_t max;
  DoneCallback final_done_callback;
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

static void fetch_range_to_sram(const char* url, bool compressed,
                                size_t first_byte, size_t size,
                                DoneCallback done_callback,
                                void* user_ctx) {
  kinetoscope.compressed = compressed;

  // This shouldn't be necessary, but in case of an incomplete compressed
  // buffer being processed before this, reset the RLE decoder now.
  rle_reset();

  fetch_range_async(url, first_byte, size,
                    http_data_to_sram,
                    done_callback,
                    user_ctx);
}

static void fetch_to_sram(const char* url, bool compressed,
                          DoneCallback done_callback,
                          void* user_ctx) {
  fetch_range_to_sram(url, compressed, 0, (size_t)-1, done_callback, user_ctx);
}

static void write_to_buffer_done(bool ok, void* user_ctx) {
  HttpBuffer* buffer = (HttpBuffer*)user_ctx;
  DoneCallback final_done_callback = buffer->final_done_callback;
  free(buffer);
  final_done_callback(ok, /* user_ctx= */ NULL);
}

static void fetch_range_to_buffer(const char* url, void* data,
                                  size_t first_byte, size_t size,
                                  DoneCallback done_callback) {
  HttpBuffer* buffer = (HttpBuffer*)malloc(sizeof(HttpBuffer));
  buffer->data = (char*)data;
  buffer->offset = 0;
  buffer->max = size;
  buffer->final_done_callback = done_callback;

  fetch_range_async(url, first_byte, size,
                    http_data_to_buffer,
                    write_to_buffer_done,
                    buffer);
}

static void fetch_to_buffer(const char* url, void* data, size_t size,
                            DoneCallback done_callback) {
  fetch_range_to_buffer(url, data, 0, size, done_callback);
}

static void stop_video() {
  // TODO: interrupt HTTP transfers
}

static size_t next_chunk_size() {
  size_t size = kinetoscope.chunk_size;
  if (kinetoscope.compressed) {
    size = kinetoscope.index.chunk_offset[kinetoscope.chunk_num + 1] -
           kinetoscope.video_url_start_byte;
  }
  return size;
}

static void fetch_chunk_done(bool ok, void* user_ctx) {
  if (ok) {
    size_t size = next_chunk_size();

    kinetoscope.chunk_num++;
    kinetoscope.chunks_left--;
    kinetoscope.video_url_start_byte += size;
    reset_sram(kinetoscope.chunk_num & 1);
  } else {
    char buf[64];
    snprintf(buf, 64, "Failed to fetch video! (chunk %d)", kinetoscope.chunk_num);
    report_error(buf);
  }

  kinetoscope.fetch_busy = false;

  if (user_ctx) {
    DoneCallback continue_callback = (DoneCallback)user_ctx;
    continue_callback(ok, /* user_ctx= */ NULL);
  }
}

static void fetch_chunk(DoneCallback done_callback) {
  // Check for underflow
  if (kinetoscope.fetch_busy) {
    report_error("Underflow detected! Internet too slow?");
    return;
  }

  // Flag that we are busy fetching.  This simulates the firmware, which can
  // only fetch one thing at a time.
  kinetoscope.fetch_busy = true;

  fetch_range_to_sram(kinetoscope.video_url,
                      kinetoscope.compressed,
                      kinetoscope.video_url_start_byte,
                      next_chunk_size(),
                      fetch_chunk_done,
                      /* user_ctx= */ done_callback);
}

static void complete_command() {
  printf("Kinetoscope: command complete.\n");
  kinetoscope.command_busy = false;
  kinetoscope.token = TOKEN_CONTROL_TO_SEGA;
}

static void start_video_0(bool ok, void* user_ctx);
static void start_video_1(bool ok, void* user_ctx);
static void start_video_2(bool ok, void* user_ctx);
static void start_video_3(bool ok, void* user_ctx);
static void start_video_4(bool ok, void* user_ctx);
static void start_video_5(bool ok, void* user_ctx);

static void start_video_async() {
  // Look up the video URL.
  uint16_t video_index = kinetoscope.arg;
  if (video_index > 127) {
    char buf[64];
    snprintf(buf, 64, "Invalid video index requested! (%d)", (int)video_index);
    report_error(buf);
    complete_command();
    return;
  }

  // Fetch the header from the appropriate section of the catalog.
  // This will have the relative URL of the full video.
  const size_t header_size = sizeof(kinetoscope.header);
  fetch_range_to_buffer(VIDEO_SERVER_CATALOG_URL, &kinetoscope.header,
                        /* first_byte= */ header_size * video_index,
                        /* size= */ header_size,
                        start_video_0);
}

static void start_video_0(bool ok, void* user_ctx) {
  uint16_t video_index = kinetoscope.arg;
  if (!ok) {
    char buf[64];
    snprintf(buf, 64, "Failed to fetch catalog index! (%d)", (int)video_index);
    report_error(buf);
    complete_command();
    return;
  }

  // Construct the full URL of the requested video from the relative one in the
  // header.
  // header.relative_url should be nul-terminated, but just in case, use
  // strnlen and fail if we get the maximum size, which would indicate no
  // nul-terminator.
  const char* path = kinetoscope.header.relative_url;
  size_t path_len = strnlen(path, sizeof(kinetoscope.header.relative_url));
  if (path_len == sizeof(kinetoscope.header.relative_url)) {
    char buf[64];
    snprintf(buf, 64, "Invalid catalog data at index! (%d)", (int)video_index);
    report_error(buf);
    complete_command();
    return;
  }

  size_t base_len = strlen(VIDEO_SERVER_BASE_URL);
  if (kinetoscope.video_url) {
    free(kinetoscope.video_url);
  }
  kinetoscope.video_url = (char*)malloc(base_len + path_len + 1);
  memcpy(kinetoscope.video_url, VIDEO_SERVER_BASE_URL, base_len);
  memcpy(kinetoscope.video_url + base_len, path, path_len);
  kinetoscope.video_url[base_len + path_len] = '\0';

  // Fetch the real header now to a buffer.
  fetch_to_buffer(kinetoscope.video_url, &kinetoscope.header,
                  sizeof(kinetoscope.header), start_video_1);
}

static void start_video_1(bool ok, void* user_ctx) {
  if (!ok) {
    report_error("Failed to fetch header!");
    complete_command();
    return;
  }

  // Manage compression.  If it's compressed, we're going to overwrite that
  // fact in memory before transferring the header data to SRAM.  We will
  // decompress it on the fly before the Sega sees it.
  kinetoscope.compressed = kinetoscope.header.compression != 0;
  kinetoscope.header.compression = 0;
  printf("Video is%s compressed!\n", kinetoscope.compressed ? "" : " not");

  if (kinetoscope.compressed) {
    // If it's compressed, fetch the chunk index to a buffer in memory.
    fetch_range_to_buffer(kinetoscope.video_url, &kinetoscope.index,
                          /* offset= */ sizeof(kinetoscope.header),
                          /* size= */ sizeof(kinetoscope.index),
                          start_video_2);
  } else {
    // If it's not compressed, move on to the next step.
    start_video_2(/* ok= */ true, /* user_ctx= */ NULL);
  }
}

static void start_video_2(bool ok, void* user_ctx) {
  if (!ok) {
    report_error("Failed to fetch index!");
    complete_command();
    return;
  }

  if (kinetoscope.compressed) {
    // Pre-byteswap the index.
    for (int i = 0; i < sizeof(kinetoscope.index) / 4; ++i) {
      kinetoscope.index.chunk_offset[i] = ntohl(kinetoscope.index.chunk_offset[i]);
    }
  }

  // Get the chunk size and number of chunks.
  kinetoscope.chunk_size = ntohl(kinetoscope.header.chunkSize);
  kinetoscope.chunks_left = ntohl(kinetoscope.header.totalChunks);
  kinetoscope.chunk_num = 0;

  // Transfer the header from emulator memory to emulated SRAM.
  reset_sram(0);
  write_sram((const uint8_t*)&kinetoscope.header, sizeof(kinetoscope.header));
  kinetoscope.video_url_start_byte = sizeof(kinetoscope.header);
  if (kinetoscope.compressed) {
    kinetoscope.video_url_start_byte = kinetoscope.index.chunk_offset[0];
  }

  // Fill the first region.
  fetch_chunk(start_video_3);
}

static void start_video_3(bool ok, void* user_ctx) {
  if (!ok) {
    report_error("Failed to fetch first chunk!");
    complete_command();
    return;
  }

  if (kinetoscope.chunks_left) {
    // Fill the second region as well.
    fetch_chunk(start_video_4);
  } else {
    start_video_4(/* ok= */ true, /* user_ctx= */ NULL);
  }
}

static void start_video_4(bool ok, void* user_ctx) {
  complete_command();
}

static void flip_region() {
  if (!kinetoscope.chunks_left) {
    return;
  }

  fetch_chunk(/* done_callback= */ NULL);
}

static void get_video_list_0(bool ok, void* user_ctx);

static void get_video_list_async() {
  printf("Kinetoscope: list\n");

  reset_sram(0);
  fetch_to_sram(VIDEO_SERVER_CATALOG_URL, /* compressed= */ false,
                get_video_list_0, /* user_ctx= */ NULL);
}

static void get_video_list_0(bool ok, void* user_ctx) {
  if (!ok) {
    report_error("Failed to download video catalog!");
  }

  complete_command();
}

static void execute_command() {
  kinetoscope.command_busy = true;

  if (kinetoscope.command == CMD_ECHO) {
    // Used by the ROM to check for the necessary streaming hardware.
    uint16_t value = kinetoscope.arg;
    printf("Kinetoscope: CMD_ECHO 0x%04x\n", value);
    reset_sram(0);
    write_sram((const uint8_t*)&value, sizeof(value));
  } else if (kinetoscope.command == CMD_LIST_VIDEOS) {
    printf("Kinetoscope: CMD_LIST_VIDEOS\n");
    get_video_list_async();
    // Because this command is async, don't fall through and complete the
    // command by returning control to the Sega.  get_video_list_async() will
    // eventually return control when its chain of callbacks terminates.
    return;
  } else if (kinetoscope.command == CMD_START_VIDEO) {
    printf("Kinetoscope: CMD_START_VIDEO\n");
    start_video_async();
    // Because this command is async, don't fall through and complete the
    // command by returning control to the Sega.  start_video_async() will
    // eventually return control when its chain of callbacks terminates.
    return;
  } else if (kinetoscope.command == CMD_STOP_VIDEO) {
    printf("Kinetoscope: CMD_STOP_VIDEO\n");
    stop_video();
  } else if (kinetoscope.command == CMD_FLIP_REGION) {
    printf("Kinetoscope: CMD_FLIP_REGION\n");
    flip_region();
  } else if (kinetoscope.command == CMD_GET_ERROR) {
    printf("Kinetoscope: CMD_GET_ERROR\n");
    write_error_to_sram();
  } else if (kinetoscope.command == CMD_CONNECT_NET) {
    printf("Kinetoscope: CMD_CONNECT_NET\n");
    // Nothing to do: already connected.
  } else if (kinetoscope.command == CMD_MARCH_TEST) {
    printf("Kinetoscope: CMD_MARCH_TEST\n");
    sram_march_test(kinetoscope.arg);
  } else {
    report_error("Unrecognized command 0x%02X!", kinetoscope.command);
  }

  complete_command();
}

void *kinetoscope_write_16(uint32_t address, void *context, uint16_t value) {
  if (address == KINETOSCOPE_PORT_COMMAND) {
    kinetoscope.command = value;
  } else if (address == KINETOSCOPE_PORT_ARG) {
    kinetoscope.arg = value;
  } else if (address == KINETOSCOPE_PORT_TOKEN) {
    // This bit is always set on write by Sega, no matter the value.
    kinetoscope.token = TOKEN_CONTROL_TO_STREAMER;
    printf("Kinetoscope: Received command 0x%02x\n", kinetoscope.command);
    // Decide when the command will execute, simulating async operation of
    // the cart's secondary processor.
    kinetoscope.ready_time = ms_now() + SIMULATED_PROCESSING_DELAY;
  } else if (address == KINETOSCOPE_PORT_ERROR) {
    printf("Kinetoscope: Clearing error bit\n");
    // This bit is always cleared on write by Sega, no matter the value.
    kinetoscope.error = 0;  // always cleared on write by Sega
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
  if (ms_now() >= kinetoscope.ready_time &&
      kinetoscope.token == TOKEN_CONTROL_TO_STREAMER &&
      !kinetoscope.command_busy) {
    execute_command();
  }

  if (address == KINETOSCOPE_PORT_TOKEN) {
    // These are always 1-bit values.
    //printf("Kinetoscope: Reading token bit: %d\n", kinetoscope.token);
    return kinetoscope.token ? 1 : 0;
  } else if (address == KINETOSCOPE_PORT_ERROR) {
    //printf("Kinetoscope: Reading error bit: %d\n", kinetoscope.error);
    // These are always 1-bit values.
    return kinetoscope.error ? 1 : 0;
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
