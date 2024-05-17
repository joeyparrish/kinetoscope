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
// FIXME: Only the ESP32 with its PSRAM can store this much at once.
#define ABOUT_3S_VIDEO_AUDIO_BYTES 901932

// A safe buffer size for these tests.
#define BUFFER_SIZE 100 * 1024

// On the ESP32 board, use PSRAM.
#if defined(ARDUINO_ARCH_ESP32)
# define malloc ps_malloc
#endif

static long test_sram_speed(uint16_t* data, int data_size) {
  // ESP32: Takes about 1200ms total, or about 2660ns per word.
  // M4: Takes about 500ms total, or about 957ns per word.  (FIXME: Can't store 1MB of data here.)
  // RPiPico: Takes about 430ms total, or about 937ns per word.  (FIXME: Can't store 1MB of data here.)
  long start = millis();
  sram_write(data, data_size);
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

static long test_download_speed(uint8_t* data, int data_size, int first_byte) {
  // FIXME: Measure on each platform
  // ESP32: Takes about 2.4s to fetch 3s worth of data, at best.
  long start = millis();
  int bytes_read = http_fetch(SERVER, /* default port */ 0, PATH,
                              first_byte, data, data_size);
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

#if defined(ARDUINO_ARCH_ESP32)
  psramInit();
#endif

  sram_init();

  // registers_init();

#if defined(ARDUINO_ARCH_ESP32)
  Client* client = internet_init_wifi(SECRET_WIFI_SSID, SECRET_WIFI_PASS);
#else
  Client* client = internet_init_wired(MAC_ADDR);
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

#if 0
  ms = test_sync_token_read_speed();
  Serial.print(ms);
  Serial.println(" us avg per sync token read.");  // 1000x reads, ms => us

  ms = test_sync_token_clear_speed();
  Serial.print(ms);
  Serial.println(" us avg per sync token clear.");  // 1000x reads, ms => us

  ms = test_register_read_speed();
  Serial.print(ms);
  Serial.println(" us avg per register read.");  // 1000x reads, ms => us
#endif

  for (int i = 0; i < 10; i++) {
    ms = test_download_speed(buffer, BUFFER_SIZE, /* first_byte= */ i);
    float bits = BUFFER_SIZE * 8.0;
    float seconds = ms / 1000.0;
    float mbps = bits / seconds / 1024.0 / 1024.0;
    Serial.print(ms);
    Serial.print(" ms to fetch one buffer (");
    Serial.print(mbps);
    Serial.println(" Mbps vs 2.75 Mbps minimum)");

    ms = test_sram_speed((uint16_t*)buffer, BUFFER_SIZE / 2); // bytes => words
    Serial.print(ms);
    Serial.println(" ms to write one buffer to SRAM.");
  }

#if 1
  while (true) { delay(1000); }
#endif
}
