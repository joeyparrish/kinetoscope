// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Firmware that runs on the microcontroller inside the cartridge.
// The microcontroller accepts commands from the player in the Sega ROM, and
// can stream video from the Internet to the cartridge's shared banks of SRAM.

#include <Arduino.h>

#include "arduino_secrets.h"
#include "error.h"
#include "http.h"
#include "internet.h"
#include "registers.h"
#include "segavideo_format.h"
#include "speed-tests.h"
#include "sram.h"
#include "string-util.h"
#include "video-server.h"

//#define DEBUG
//#define RUN_TESTS

#if !defined(SECRET_WIFI_SSID)
# define SECRET_WIFI_SSID ""
#endif

#if !defined(SECRET_WIFI_PASS)
# define SECRET_WIFI_PASS ""
#endif

#define MAX_SERVER 256
#define MAX_PATH 256
#define MAX_FETCH_SIZE (1024 * 1024)

#define NETWORK_TIMEOUT_SECONDS 30

// Macro required by rle-common.h:
#define SRAM_WRITE(buffer, size) sram_write(buffer, size)
#include "rle-common.h"

// Allocate a second 8kB stack for the second core.
// https://github.com/earlephilhower/arduino-pico/blob/master/docs/multicore.rst
bool core1_separate_stack = true;

// An actual MAC address assigned to me with my ethernet board.
// Don't put two of these devices on the same network, y'all.
static const uint8_t MAC_ADDR[] = { 0x98, 0x76, 0xB6, 0x12, 0xD4, 0x9E };

static void freeze() {
  while (true) { delay(1000 /* ms */); }
}

// The second core waits on this variable before beginning its loop.
static bool hardware_ready = false;

// The second core uses these to receive commands from the first core.
static volatile bool second_core_idle = true;
static volatile bool second_core_interrupt = false;
static volatile bool fetch_okay = false;
static int fetch_start_byte = 0;
static int fetch_size = 0;
static char fetch_path[MAX_PATH];
static http_data_callback fetch_callback = NULL;
static uint8_t* fetch_buffer = NULL;
static int fetch_buffer_size = 0;
static SegaVideoIndex video_index;

// Also read by speed tests
bool network_connected = false;

static int chunk_size = 0;
static int total_chunks = 0;
static bool is_compressed = false;
static int next_chunk_num = 0;
static int next_offset = 0;
static int next_size = 0;

static void init_all_hardware() {
  registers_init();
  sram_init();

  // Use LED as a primitive visual status.
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.println("All hardware initialized.");
}

static void connect_network() {
  // Prefer wired, fall back to WiFi if configured.
  Serial.println("Connecting to the network...");
  Client* client = internet_init_wired(MAC_ADDR, NETWORK_TIMEOUT_SECONDS);

  if (!client) {
    Serial.println("Wired connection failed!");

    bool has_wifi = false;
#if defined(ARDUINO_ARCH_RP2040)
    has_wifi = rp2040.isPicoW();
#endif

    if (!has_wifi) {
      Serial.println("WiFi hardware not available!");
      report_error("Wired connection failed and WiFi hardware not available!");
    } else if (!strlen(SECRET_WIFI_SSID)) {
      Serial.println("WiFi not configured!");
      report_error("Wired connection failed and WiFi not configured!");
    } else {
      client = internet_init_wifi(SECRET_WIFI_SSID, SECRET_WIFI_PASS,
                                  NETWORK_TIMEOUT_SECONDS);
      if (!client) {
        Serial.println("WiFi connection failed!");
        report_error("WiFi connection failed!");
      }
    }
  }

  if (!client) {
    Serial.println("Failed to connect to the network!");
  }
  http_init(client);
  network_connected = client != NULL;
}

// Also called by speed tests
bool http_sram_callback(const uint8_t* buffer, int bytes) {
  // Check for interrupt.
  if (second_core_interrupt) {
    return false;
  }

  sram_write(buffer, bytes);
  return true;
}

// Also called by speed tests
bool http_rle_sram_callback(const uint8_t* buffer, int bytes) {
  // Check for interrupt.
  if (second_core_interrupt) {
    return false;
  }

  rle_to_sram(buffer, bytes);
  return true;
}

// Also called by speed tests
void http_rle_reset() {
  rle_reset();
}

static bool http_buffer_callback(const uint8_t* buffer, int bytes) {
  // Check for interrupt.
  if (second_core_interrupt) {
    return false;
  }

  int to_copy = min(bytes, fetch_buffer_size);
  memcpy(fetch_buffer, buffer, to_copy);
  fetch_buffer += to_copy;
  fetch_buffer_size -= to_copy;
  return true;
}

// Expects fetch_callback and any necessary globals for it to be set in advance.
static bool fetch_generic(const char* path, int start_byte, int size) {
  if (!second_core_idle) {
    report_error("Command conflict! Busy!");
    return false;
  }

  if (path != fetch_path) {
    copy_string(fetch_path, path, MAX_PATH);
  }
  fetch_start_byte = start_byte;
  fetch_size = size;

  second_core_idle = false;
  return true;
}

static bool fetch_into_buffer(void* buffer, const char* path,
                              int start_byte, int size) {
  fetch_callback = http_buffer_callback;
  fetch_buffer = (uint8_t*)buffer;
  fetch_buffer_size = size;
  return fetch_generic(path, start_byte, size);
}

static bool fetch_into_sram(const char* path, int start_byte = 0,
                            int size = MAX_FETCH_SIZE,
                            bool decompress = false) {
  fetch_callback = decompress ? http_rle_sram_callback : http_sram_callback;
  return fetch_generic(path, start_byte, size);
}

static bool await_fetch() {
  while (!second_core_idle) { delay(1 /* ms */); }
  return fetch_okay;
}

static void process_command(uint8_t command, uint8_t arg) {
  Serial.print("Command ");
  Serial.print(command);
  Serial.print(" arg 0x");
  Serial.print(arg < 0x10 ? "0" : "");
  Serial.println(arg, HEX);

  switch (command) {
    case KINETOSCOPE_CMD_ECHO:
      // Write the argument to SRAM so the ROM software knows we are listening.
      sram_start_bank(0);
      sram_write(&arg, sizeof(arg));
      sram_flush_and_release_bank();
      break;

    case KINETOSCOPE_CMD_LIST_VIDEOS:
      // Pull video list into SRAM.
      Serial.println("Fetching video list...");
      sram_start_bank(0);
      if (fetch_into_sram(VIDEO_CATALOG_PATH) && await_fetch()) {
        Serial.println("Done.");
      }
      break;

    case KINETOSCOPE_CMD_START_VIDEO:
      Serial.print("Starting video ");
      Serial.println(arg);

      // Get the appropriate header from the catalog.
      SegaVideoHeader header;
      if (!fetch_into_buffer(&header, VIDEO_CATALOG_PATH, arg * sizeof(header),
                             sizeof(header)) ||
          !await_fetch()) {
        break;
      }

      // Construct the URL of the video.
      copy_string(fetch_path, VIDEO_SERVER_BASE_PATH, MAX_PATH);
      concatenate_string(fetch_path, header.relative_url, MAX_PATH);

      // Start streaming.
      chunk_size = ntohl(header.chunkSize);
      total_chunks = ntohl(header.totalChunks);
      is_compressed = header.compression != 0;

      // Since we decompress it in firmware, the Sega sees it as uncompressed.
      header.compression = 0;

      if (is_compressed) {
        // Fetch the index into memory, only if compressed.
        if (!fetch_into_buffer(&video_index, fetch_path, sizeof(header),
                               sizeof(video_index)) ||
            !await_fetch()) {
          break;
        }

        // Pre-byteswap the whole index.
        for (int i = 0; i < sizeof(video_index) / 4; ++i) {
          video_index.chunk_offset[i] = ntohl(video_index.chunk_offset[i]);
        }
      }

      // Fill both SRAM banks before returning.
      sram_start_bank(0);
      sram_write((const uint8_t*)&header, sizeof(header));
      // NOTE: Always omit the video index in SRAM!
      next_chunk_num = 0;

      if (is_compressed) {
        next_offset = video_index.chunk_offset[next_chunk_num];
        next_size = video_index.chunk_offset[next_chunk_num + 1] - next_offset;
      } else {
        next_offset = sizeof(header) + sizeof(video_index);
        next_size = chunk_size;
      }
      if (!fetch_into_sram(fetch_path, next_offset, next_size, is_compressed) ||
          !await_fetch()) {
        break;
      }
      next_chunk_num++;
      next_offset += next_size;

      if (total_chunks != 1) {
        sram_start_bank(1);
        if (is_compressed) {
          next_size =
              video_index.chunk_offset[next_chunk_num + 1] - next_offset;
        } else {
          next_size = chunk_size;
        }
        if (!fetch_into_sram(fetch_path, next_offset, next_size,
                             is_compressed) ||
            !await_fetch()) {
          break;
        }
        next_chunk_num++;
        next_offset += next_size;
      }
      break;

    case KINETOSCOPE_CMD_STOP_VIDEO:
      if (!second_core_idle) {
        // Stop streaming.  Interrupt any download in progress.
        second_core_interrupt = true;
        // Wait for recognition of the interrupt.
        while (!second_core_idle || second_core_interrupt) {
          delay(1 /* ms */);
        }
      }
      break;

    case KINETOSCOPE_CMD_FLIP_REGION:
      if (next_chunk_num >= total_chunks) {
        // Nothing to do.  EOF.
        break;
      }

      if (!second_core_idle) {
        report_error("Buffer underflow!  Internet too slow?");
        break;
      }

      // Start filling the next SRAM bank.  Don't wait for completion.
      sram_start_bank(next_chunk_num & 1);
      if (is_compressed) {
        next_size = video_index.chunk_offset[next_chunk_num + 1] - next_offset;
      } else {
        next_size = chunk_size;
      }
      fetch_into_sram(fetch_path, next_offset, next_size, is_compressed);
      next_chunk_num++;
      next_offset += next_size;
      break;

    case KINETOSCOPE_CMD_GET_ERROR:
      // Write a buffered error message to SRAM so the ROM software can read it.
      write_error_to_sram();
      break;

    case KINETOSCOPE_CMD_CONNECT_NET:
      if (!network_connected) {
        connect_network();
      }
      break;

    case KINETOSCOPE_CMD_MARCH_TEST:
      sram_march_test(arg);
      break;

    default: {
      report_error("Unrecognized command 0x%02X!", command);
      break;
    }
  }

#ifdef DEBUG
  // Don't clear the flag too quickly.
  // TODO: Determine if this is necessary once hardware prototypes are working.
  delay(10 /* ms */);
#endif

  clear_cmd();
  Serial.println("Command complete.");
}

// Setup and loop for the first core.  This core will initialize all hardware
// and then process incoming commands from the Sega.

void setup() {
  init_all_hardware();

  Serial.begin(115200);
  Serial.println("Kinetoscope boot!\n");

#ifdef RUN_TESTS
  // Wait for Serial.  No point running tests if we can't see the output.
  while (!Serial) { delay(1); }

  // Automatically connect to the network to run speed tests.
  connect_network();

  // Run tests.
  run_tests();
#endif

  // Allow the second core to start its loop.
  hardware_ready = true;
}

void loop() {
  if (!is_cmd_set()) {
    return;
  }

  uint8_t command = read_register(KINETOSCOPE_REG_CMD);
  uint8_t arg = read_register(KINETOSCOPE_REG_ARG);
  process_command(command, arg);
}

// Setup and loop for the second core.  These methods may be specific to the
// RP2040 core, so they may need to be ported if anyone wants to use a
// different microcontroller.

void setup1() {
  // Wait for the first core to finish initializing the hardware.
  while (!hardware_ready) { delay(1 /* ms */); }
}

void loop1() {
  if (second_core_idle) {
    return;
  }

  // Begin requested transfer.  The callback will check for interrupts via
  // second_core_interrupt.  The http library will report an error to the Sega
  // if it fails.
#ifdef DEBUG
  Serial.print("Fetching ");
  Serial.print(fetch_path);
  Serial.print(" at ");
  Serial.println(fetch_start_byte);
#endif

  digitalWrite(LED_BUILTIN, HIGH);
  fetch_okay = http_fetch(VIDEO_SERVER, VIDEO_SERVER_PORT, fetch_path,
                          fetch_start_byte, fetch_size, fetch_callback);
  // It's fine to do this, even if fetch_callback != http_sram_callback.
  // This way, SRAM is always flushed even when the first core doesn't await
  // the fetch.
  sram_flush_and_release_bank();
  digitalWrite(LED_BUILTIN, LOW);

  // Clear state.
  second_core_interrupt = false;
  second_core_idle = true;
}
