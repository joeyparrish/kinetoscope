// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Firmware that runs on the microcontroller inside the cartridge.
// The microcontroller accepts commands from the player in the Sega ROM, and
// can stream video from the Internet to the cartridge's shared banks of SRAM.

#include <Arduino.h>
#include <HardwareSerial.h>

#include "arduino_secrets.h"
#include "error.h"
#include "http.h"
#include "internet.h"
#include "registers.h"
#include "speed-tests.h"
#include "sram.h"
#include "string-util.h"

#define DEBUG
#define RUN_TESTS

// NOTE: This must be a plain HTTP server.  HTTPS is too expensive for this
// application and microcontroller.  Even though we could do it, it would hurt
// our slim throughput margins too much.
#define VIDEO_SERVER "storage.googleapis.com"
#define VIDEO_SERVER_PORT 80
// TODO: Finalize the metadata format and upload this.
#define VIDEO_LIST_PATH "/sega-kinetoscope/canned-videos/list.txt"

#define MAX_SERVER 256
#define MAX_PATH 256
#define MAX_FETCH_SIZE (1024 * 1024)

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
static char fetch_server[MAX_SERVER];
static volatile uint16_t fetch_port = 0;
static char fetch_path[MAX_PATH];
static volatile int fetch_start_byte = 0;
static volatile int fetch_size = 0;

static void init_all_hardware() {
  sram_init();
  registers_init();

  // Turn on LED as a primitive visual status.
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // Prefer wired, fall back to WiFi if configured.
  Client* client = internet_init_wired(MAC_ADDR);

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
      client = internet_init_wifi(SECRET_WIFI_SSID, SECRET_WIFI_PASS);
      if (!client) {
        Serial.println("WiFi connection failed!");
        report_error("WiFi connection failed!");
      }
    }
  }

  if (!client) {
    Serial.println("Failed to connect to the network!");
  } else {
    http_init(client);
  }
}

static void second_core_fetch(const char* server, uint16_t port,
                              const char* path, int start_byte = 0,
                              int size = MAX_FETCH_SIZE) {
  copy_string(fetch_server, server, MAX_SERVER);
  fetch_port = port;
  copy_string(fetch_path, path, MAX_PATH);
  fetch_start_byte = start_byte;
  fetch_size = size;
  second_core_idle = false;
}

static void await_second_core_fetch() {
  while (!second_core_idle) { delay(1 /* ms */); }
}

static void process_command(uint8_t command, uint8_t arg) {
  switch (command) {
    case KINETOSCOPE_CMD_ECHO:
      // Write the argument to SRAM so the ROM software knows we are listening.
      sram_start_bank(0);
      sram_write(&arg, sizeof(arg));
      sram_flush();
      break;

    case KINETOSCOPE_CMD_LIST_VIDEOS:
      if (!second_core_idle) {
        report_error("Command conflict! Busy!");
        break;
      }

      // Pull video list into SRAM.
      Serial.println("Fetching video list...");
      second_core_fetch(VIDEO_SERVER, VIDEO_SERVER_PORT, VIDEO_LIST_PATH);
      await_second_core_fetch();
      Serial.println("Done.");
      break;

    case KINETOSCOPE_CMD_START_VIDEO:
      if (!second_core_idle) {
        report_error("Command conflict! Busy!");
        break;
      }

      // TODO: Start streaming.  Fill both SRAM banks before returning.
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
      if (!second_core_idle) {
        report_error("Buffer underflow!  Internet too slow?");
        break;
      }

      // TODO: Start filling the next SRAM bank.
      break;

    case KINETOSCOPE_CMD_GET_ERROR:
      // Write a buffered error message to SRAM so the ROM software can read it.
      write_error_to_sram();
      break;
  }

  clear_cmd();
}

// Setup and loop for the first core.  This core will initialize all hardware
// and then process incoming commands from the Sega.

void setup() {
  Serial.begin(115200);

#ifdef DEBUG
  while (!Serial) { delay(10 /* ms */); }  // Wait for serial port to connect

  // Delay startup so we can have the serial monitor attached.
  delay(1000 /* ms */);
  Serial.println("Kinetoscope boot!\n");
#endif

  init_all_hardware();

#ifdef RUN_TESTS
  if (!is_error_flagged()) {
    run_tests();
  }
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

static bool http_transfer_callback(const uint8_t* buffer, int bytes) {
  // Check for interrupt.
  if (second_core_interrupt) {
    return false;
  }

  sram_write(buffer, bytes);
  return true;
}

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
  http_fetch(fetch_server, fetch_port, fetch_path, fetch_start_byte,
             fetch_size, http_transfer_callback);

  // Clear state.
  second_core_interrupt = false;
  second_core_idle = true;
}
