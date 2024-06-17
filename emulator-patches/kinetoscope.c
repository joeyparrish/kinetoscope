// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Emulation of Kinetoscope video streaming hardware in BlastEm.

#include <stdint.h>
#include <string.h>

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

// TODO: Get this from disk instead
#define CANNED_VIDEO_LIST \
    "Never Gonna Give You Up\n" \
    "Zoey Ann the Boxer\n" \
    "Gangnam Style\n" \
    "Bohemian Rhapsody\n" \
    "Developers, Developers\n" \
    "Shia LaBeouf\n" \

// Video indices from the list above
#define NEVER_GONNA_GIVE_YOU_UP 0
#define ZOEY_ANN_THE_BOXER      1
#define GANGNAM_STYLE           2
#define BOHEMIAN_RHAPSODY       3
#define DEVELOPERS_DEVELOPERS   4
#define SHIA_LABEOUF            5

// SRAM regions.
#define REGION_OFFSET_MASK 0x100000  // 0MB or 1MB offset
#define REGION_SIZE        0x100000  // 1MB size

// Offset of paddingBytes field from the end of the headers
#define PADDING_BYTES_OFFSET_FROM_END_OF_HEADER \
  (sizeof(SegaVideoChunkHeader) - offsetof(SegaVideoChunkHeader, paddingBytes))


static uint8_t* global_sram_buffer = NULL;
static uint32_t global_sram_size = 0;
static uint16_t global_command = 0;
static uint16_t global_token = TOKEN_CONTROL_TO_SEGA;
static uint16_t global_error = 0;
static uint16_t global_arg;
static uint32_t global_ready_cycle = (uint32_t)-1;
static uint8_t* global_video_data = NULL;

// Where we read from next:
static uint32_t global_video_data_offset;
// Where we write to next:
static uint32_t global_sram_offset;
// Size of the last chunk written:
static uint32_t global_last_chunk_size;

const char* get_video_dir() {
  static const char* video_dir = NULL;
  if (!video_dir) {
    const char* base = get_userdata_dir();
    if (base) {
      video_dir = alloc_concat(base, PATH_SEP "Kinetoscope-Emulation");
    }
  }
  return video_dir;
}

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

static void stop_video() {
  if (global_video_data) {
    free(global_video_data);
  }
  global_video_data = NULL;
}

static void fill_region() {
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
}

static void start_video() {
  char* video_path = NULL;

  switch (global_arg) {
#define VIDEO_CASE(VALUE_MACRO) \
    case VALUE_MACRO: \
      video_path = alloc_concat( \
          get_video_dir(), PATH_SEP #VALUE_MACRO ".segavideo"); \
      break;

    VIDEO_CASE(NEVER_GONNA_GIVE_YOU_UP);
    VIDEO_CASE(ZOEY_ANN_THE_BOXER);
    VIDEO_CASE(GANGNAM_STYLE);
    VIDEO_CASE(BOHEMIAN_RHAPSODY);
    VIDEO_CASE(DEVELOPERS_DEVELOPERS);
    VIDEO_CASE(SHIA_LABEOUF);
  }

  if (!video_path) {
    // Write a 0 to SRAM, which will make sure any old data doesn't look like a
    // valid video.
    write_sram(0, (const uint8_t*)"", 1);
    printf("Kinetoscope: Unknown video (%d)!\n", global_arg);
    return;
  }

  FILE* f = fopen(video_path, "r");
  if (!f) {
    printf("Kinetoscope: Failed to open video (%s)!\n", video_path);
    free(video_path);
    return;
  }

  long size;
  if (fseek(f, 0, SEEK_END) ||
      ((size = ftell(f)) < 0) ||
      fseek(f, 0, SEEK_SET)) {
    printf("Kinetoscope: Failed to get video size (%s)!\n", video_path);
    free(video_path);
    return;
  }

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
}

static void flip_region() {
  if (!global_video_data) {
    printf("Kinetoscope: FLIP_REGION command while not playing!\n");
    return;
  }

  fill_region();
}

static void get_video_list() {
  uint8_t page_num = global_arg >> 8;
  uint8_t page_size = global_arg & 0xff;
  printf("VideoStream: list page_num = %d, page_size = %d\n",
      page_num, page_size);

  // Find the requested page.
  const char* video_list = CANNED_VIDEO_LIST;
  for (int i = 0; i < page_num * page_size; ++i) {
    video_list = index(video_list, '\n');
    if (!video_list) break;
    video_list += 1;  // Move past the newline.
  }

  if (video_list) {
    // Include the NUL terminator in the length.
    uint32_t video_list_len = strlen(video_list) + 1;
    // The list on disk might be longer than what we can fit in SRAM.
    if (video_list_len > REGION_SIZE) {
      video_list_len = REGION_SIZE - 1;
    }

    // Write the requested page to SRAM.
    write_sram(0, (const uint8_t*)video_list, video_list_len);
  } else {
    // The page was not found, so write a blank string.
    printf("Kinetoscope: Unable to find requested page!\n");
    write_sram(0, (const uint8_t*)"", 1);
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
    // This bit is always cleared on write by Sega, no matter the value.
    global_error = 0;  // always cleared on write by Sega
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
    return global_token ? 1 : 0;
  } else if (address == KINETOSCOPE_PORT_ERROR) {
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
