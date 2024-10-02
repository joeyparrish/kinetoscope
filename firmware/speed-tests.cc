// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Microcontroller function speed tests.

#include <Arduino.h>
#include <HardwareSerial.h>

#include "fast-gpio.h"
#include "http.h"
#include "registers.h"
#include "segavideo_format.h"
#include "sram.h"

#define SERVER "storage.googleapis.com"
#define RAW_VIDEO_PATH   "/sega-kinetoscope/canned-videos/Never%20Gonna%20Give%20You%20Up.segavideo"

// 3s chunk of audio+video data, at default settings, without main headers
#define ABOUT_3S_VIDEO_AUDIO_BYTES 901376

// A safe buffer size for these tests.
#define BUFFER_SIZE 100 * 1024

// Explicitly unrolled loop for 10 repeated statements.
#define X10(a) { a; a; a; a; a;  a; a; a; a; a; }
// Explicitly unrolled loop for 100 repeated statements.
#define X100(a) { X10(X10(a)); }
// Explicitly unrolled loop for 1k repeated statements.
#define X1k(a) { X10(X10(X10(a))); }
// A partially unrolled loop that minimizes time spent on incrementing and
// checking, while not exploding the program size to the point that it slows
// down execution (10k/100k unrolled) or overruns the available program space
// (1M unrolled).
#define X1M(a) { for (int i = 0; i < 1'000; ++i) { X1k(a); } }

static long test_fast_gpio_speed() {
  // ~75 ns per pulse
  long start = millis();
  X1M(FAST_PULSE_ACTIVE_LOW(SYNC_PIN__CMD_CLEAR));
  long end = millis();
  return end - start;
}

static long test_sync_token_read_speed() {
  // ~86 ns per read
  long start = millis();
  X1M(is_cmd_set());
  long end = millis();
  return end - start;
}

static long test_sync_token_clear_speed() {
  // ~122 ns per clear
  long start = millis();
  X1M(clear_cmd());
  long end = millis();
  return end - start;
}

static long test_register_read_speed() {
  // ~1543 ns per read
  long start = millis();
  X1M(read_register(i & 3));
  long end = millis();
  return end - start;
}

static long test_sram_speed() {
  // 100kB: ~116ms
  // 1MB: ~1160ms
  // 3s video+audio: ~1020ms
  // Rather than allocate a buffer, just write out 100kB of instructions.
  uint8_t* buffer = (uint8_t*)main;
  long start = millis();
  sram_start_bank(0);
  sram_write(buffer, BUFFER_SIZE);
  sram_flush_and_release_bank();
  long end = millis();
  return end - start;
}

static uint8_t* http_local_buffer = NULL;
static bool http_local_buffer_callback(const uint8_t* buffer, int bytes) {
  memcpy(http_local_buffer, buffer, bytes);
  http_local_buffer += bytes;
  return true;
}

// Linked from firmware.ino:
extern bool http_sram_callback(const uint8_t* buffer, int bytes);

static long test_raw_download_speed() {
  // 2.5Mbps minimum required
  // ~2.7Mbps with initial HTTP connection overhead
  // ~3.0Mbps on subsequent requests
  long start = millis();
  sram_start_bank(0);
  if (!http_fetch(SERVER,
                  /* default port */ 0,
                  RAW_VIDEO_PATH,
                  /* first byte */ 0,
                  /* total_size= */ ABOUT_3S_VIDEO_AUDIO_BYTES,
                  http_sram_callback)) {
    Serial.println("Fetch failed!");
  }
  sram_flush_and_release_bank();
  long end = millis();
  return end - start;
}

void run_tests() {
  long ms;

  ms = test_fast_gpio_speed();
  Serial.print(ms);
  Serial.println(" ns avg per GPIO pulse.");  // 1Mx pulses, ms => ns

  ms = test_sync_token_read_speed();
  Serial.print(ms);
  Serial.println(" ns avg per sync token read.");  // 1Mx reads, ms => ns

  ms = test_sync_token_clear_speed();
  Serial.print(ms);
  Serial.println(" ns avg per sync token clear.");  // 1Mx reads, ms => ns

  ms = test_register_read_speed();
  Serial.print(ms);
  Serial.println(" ns avg per register read.");  // 1Mx reads, ms => ns

  ms = test_sram_speed();
  Serial.print(ms);
  Serial.print(" ms to write ");
  Serial.print(BUFFER_SIZE);
  Serial.println(" bytes to SRAM");

  if (is_error_flagged()) {
    Serial.println("Error flagged, skipping network tests.");
  } else {
    Serial.println("Beginning raw network tests.");

    for (int i = 0; i < 10; i++) {
      ms = test_raw_download_speed();
      float bits = ABOUT_3S_VIDEO_AUDIO_BYTES * 8.0;
      float seconds = ms / 1000.0;
      float mbps = bits / seconds / 1024.0 / 1024.0;
      Serial.print(ms);
      Serial.print(" ms to stream ~3s raw video to SRAM (");
      Serial.print(mbps);
      Serial.println(" Mbps vs 2.50 Mbps minimum)");
    }
  }

  Serial.println("\n");
}
