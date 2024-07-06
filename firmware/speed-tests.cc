// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Microcontroller function speed tests.

#include <Arduino.h>
#include <HardwareSerial.h>

#include "http.h"
#include "registers.h"
#include "sram.h"

#define SERVER "storage.googleapis.com"
#define PATH   "/sega-kinetoscope/canned-videos/Never%20Gonna%20Give%20You%20Up.segavideo"

// 3s worth of audio+video data, at default settings, with headers.
#define ABOUT_3S_VIDEO_AUDIO_BYTES 901376

// A safe buffer size for these tests.
#define BUFFER_SIZE 100 * 1024

static long test_sram_speed(const uint8_t* buffer, int bytes) {
  // 100kB: ~83ms
  // 1MB: ~830ms
  // 3s video+audio: ~731ms
  long start = millis();
  sram_start_bank(0);
  sram_write(buffer, bytes);
  sram_flush();
  long end = millis();
  return end - start;
}

static long test_sync_token_read_speed() {
  // ~114 ns per read
  long start = millis();
  int count = 0;
  for (int i = 0; i < 1000000; ++i) {
    count += is_cmd_set();
  }
  long end = millis();
  return end - start;
}

static long test_sync_token_clear_speed() {
  // ~114 ns per clear
  long start = millis();
  for (int i = 0; i < 1000000; ++i) {
    clear_cmd();
  }
  long end = millis();
  return end - start;
}

static long test_register_read_speed() {
  // ~228 ns per read
  int count = 0;
  long start = millis();
  for (int i = 0; i < 1000000; ++i) {
    count += read_register(i & 3);
  }
  long end = millis();
  return end - start;
}

static bool sram_write_callback(const uint8_t* buffer, int bytes) {
  sram_write(buffer, bytes);
  return true;  // HTTP transfer can continue.
}

static long test_download_speed(int first_byte, int total_size) {
  // 2.5Mbps minimum required
  // ~2.7Mbps with initial HTTP connection overhead
  // ~3.0Mbps on subsequent requests @902kB
  long start = millis();
  sram_start_bank(0);
  int bytes_read = http_fetch(SERVER, /* default port */ 0, PATH,
                              first_byte, total_size,
                              sram_write_callback);
  sram_flush();
  long end = millis();
  return end - start;
}

void run_tests() {
  long ms;

  uint8_t* buffer = NULL;
  buffer = (uint8_t*)malloc(BUFFER_SIZE);
  if (!buffer) {
    Serial.println("Failed to allocate buffer!");
    while (true) { delay(1000); }
  }

  ms = test_sync_token_read_speed();
  Serial.print(ms);
  Serial.println(" ns avg per sync token read.");  // 1Mx reads, ms => ns

  ms = test_sync_token_clear_speed();
  Serial.print(ms);
  Serial.println(" ns avg per sync token clear.");  // 1Mx reads, ms => ns

  ms = test_register_read_speed();
  Serial.print(ms);
  Serial.println(" ns avg per register read.");  // 1Mx reads, ms => ns

  ms = test_sram_speed(buffer, BUFFER_SIZE);
  Serial.print(ms);
  Serial.print(" ms to write ");
  Serial.print(BUFFER_SIZE);
  Serial.println(" bytes to SRAM");

  for (int i = 0; i < 10; i++) {
    ms = test_download_speed(/* first_byte= */ i, ABOUT_3S_VIDEO_AUDIO_BYTES);
    float bits = ABOUT_3S_VIDEO_AUDIO_BYTES * 8.0;
    float seconds = ms / 1000.0;
    float mbps = bits / seconds / 1024.0 / 1024.0;
    Serial.print(ms);
    Serial.print(" ms to stream ~3s video to SRAM (");
    Serial.print(mbps);
    Serial.println(" Mbps vs 2.50 Mbps minimum)");
  }

  Serial.println("\n");
  free(buffer);
}
