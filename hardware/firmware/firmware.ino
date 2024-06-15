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
#include "http.h"
#include "internet.h"
#include "registers.h"
#include "sram.h"

#define SERVER "storage.googleapis.com"
#define PATH   "/sega-kinetoscope/canned-videos/NEVER_GONNA_GIVE_YOU_UP.segavideo"

// An actual MAC address assigned to me with my ethernet board.
// Don't put two of these devices on the same network, y'all.
uint8_t MAC_ADDR[] = { 0x98, 0x76, 0xB6, 0x12, 0xD4, 0x9E };

// ~3s worth of audio+video data with headers and worst-case padding.
#define ABOUT_3S_VIDEO_AUDIO_BYTES 901932

// A safe buffer size for these tests.
#define BUFFER_SIZE 100 * 1024

// FIXME: Update all measurements

static long test_sram_speed(uint8_t* buffer, int bytes) {
  // Takes about 430ms total, or about 937ns per word.
  long start = millis();
  sram_start_bank(0);
  sram_write(buffer, bytes);
  sram_flush();
  long end = millis();
  return end - start;
}

static long test_sync_token_read_speed() {
  long start = millis();
  int count = 0;
  // Takes about 470us each.
  for (int i = 0; i < 1000; ++i) {
    count += is_sync_token_set();
  }
  long end = millis();
  return end - start;
}

static long test_sync_token_clear_speed() {
  long start = millis();
  // Takes about 700us each.
  for (int i = 0; i < 1000; ++i) {
    clear_sync_token();
  }
  long end = millis();
  return end - start;
}

static long test_register_read_speed() {
  int count = 0;
  long start = millis();
  // Takes about 820us each.
  for (int i = 0; i < 1000; ++i) {
    count += read_register(i & 3);
  }
  long end = millis();
  return end - start;
}

static long test_download_speed(int first_byte, int total_size) {
  // FIXME: Measure on each platform
  long start = millis();
  sram_start_bank(0);
  int bytes_read = http_fetch(SERVER, /* default port */ 0, PATH,
                              first_byte, total_size,
                              sram_write);
  sram_flush();
  long end = millis();
  return end - start;
}

uint8_t* buffer;

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }  // Wait for serial port to connect

  // Delay startup so we can have the serial monitor attached.
  delay(1000);
  Serial.println("Hello, world!\n");

  sram_init();
  registers_init();

  // FIXME: Prefer wired, fall back to wifi if configured
  Client* client = internet_init_wired(MAC_ADDR);
#if defined(ARDUINO_ARCH_RP2040)
  //Client* client = internet_init_wifi(SECRET_WIFI_SSID, SECRET_WIFI_PASS);
#endif
  http_init(client);

  pinMode(LED_BUILTIN, OUTPUT);

  buffer = (uint8_t*)malloc(BUFFER_SIZE);
  if (!buffer) {
    Serial.println("Failed to allocate test buffer!");
    while (true) { delay(1000); }
  }

  Serial.println("\n");
}

void loop() {
  long ms;

  ms = test_sync_token_read_speed();
  Serial.print(ms);
  Serial.println(" us avg per sync token read.");  // 1000x reads, ms => us

  ms = test_sync_token_clear_speed();
  Serial.print(ms);
  Serial.println(" us avg per sync token clear.");  // 1000x reads, ms => us

  ms = test_register_read_speed();
  Serial.print(ms);
  Serial.println(" us avg per register read.");  // 1000x reads, ms => us

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

  while (true) { delay(1000); }
}
