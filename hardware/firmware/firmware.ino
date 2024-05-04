// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Firmware that runs on the Adafruit ESP32 V2 Feather inside the cartridge.
// The feather accepts commands from the player in the Sega ROM, and can stream
// video from WiFi to the cartridge's shared banks of SRAM.

#include <Arduino.h>
#include <HardwareSerial.h>

#include <esp32-hal-psram.h>

#include "arduino_secrets.h"
#include "registers.h"
#include "sram.h"
#include "wifi.h"

#define SERVER "storage.googleapis.com"
#define PATH   "/sega-kinetoscope/canned-videos/NEVER_GONNA_GIVE_YOU_UP.segavideo"

// ~3s worth of audio+video data with headers and worst-case padding.
#define ABOUT_3S_VIDEO_AUDIO_BYTES 901932

static long test_sram_speed(uint16_t* data, int data_size) {
  // Takes about 1200ms total, or about 2660ns per word.
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

static long test_wifi_speed(uint8_t* data, int data_size, int first_byte) {
  // Takes about 2.4s to fetch 3s worth of data.  FIXME: too slow!
  long start = millis();
  int bytes_read = wifi_https_fetch(SERVER, /* default port */ 0, PATH,
                                    first_byte, data, data_size);
  long end = millis();
  return end - start;
}

uint8_t* wifi_buffer;
uint16_t* sram_buffer;

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }  // Wait for serial port to connect

  // Delay startup so we can have the serial monitor attached.
  delay(1000);
  Serial.println("\n");

  psramInit();

  sram_init();

  registers_init();

  wifi_init(SECRET_WIFI_SSID, SECRET_WIFI_PASS);

  pinMode(LED_BUILTIN, OUTPUT);

  wifi_buffer = (uint8_t*)ps_malloc(ABOUT_3S_VIDEO_AUDIO_BYTES);
  if (!wifi_buffer) {
    Serial.println("Failed to allocate WiFi buffer in PSRAM!\n");
    while (true) { delay(1000); }
  }

  sram_buffer = (uint16_t*)ps_malloc(ABOUT_3S_VIDEO_AUDIO_BYTES);
  if (!sram_buffer) {
    Serial.println("Failed to allocate SRAM buffer in PSRAM!\n");
    while (true) { delay(1000); }
  }

  Serial.println("\n");
}

volatile int tasks_done = 0;
volatile long task0_ms = 0;
volatile long task1_ms = 0;

void task_core0(void* params) {
  task0_ms = test_wifi_speed(wifi_buffer, ABOUT_3S_VIDEO_AUDIO_BYTES,
                             /* first_byte= */ 0);
  tasks_done++;
  vTaskDelete(NULL);
}

void task_core1(void* params) {
  task1_ms = test_sram_speed(sram_buffer,
                             ABOUT_3S_VIDEO_AUDIO_BYTES / 2); // bytes => words
  tasks_done++;
  vTaskDelete(NULL);
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

  ms = test_wifi_speed(wifi_buffer, ABOUT_3S_VIDEO_AUDIO_BYTES,
                       /* first_byte= */ 0);
  Serial.print(ms);
  Serial.println(" ms to fetch 3s of data over HTTP (no multitasking).");

  ms = test_sram_speed(sram_buffer,
                       ABOUT_3S_VIDEO_AUDIO_BYTES / 2); // bytes => words
  Serial.print(ms);
  Serial.println(" ms to write 3s of data to SRAM (no multitasking).");

  tasks_done = 0;

  TaskHandle_t task0;
  TaskHandle_t task1;

  xTaskCreatePinnedToCore(
      task_core0, "task_core0",
      10000, NULL, 1,
      &task0, 0);

  xTaskCreatePinnedToCore(
      task_core1, "task_core1",
      10000, NULL, 1,
      &task1, 1);

  long start = millis();
  while (tasks_done != 2) {
    delay(100);
  }
  long end = millis();

  Serial.print(task0_ms);
  Serial.println(" ms to fetch 3s of data over HTTP (multitasking).");

  Serial.print(task1_ms);
  Serial.println(" ms to write 3s of data to SRAM (multitasking).");

  Serial.print(end - start);
  Serial.println(" ms overall for both tasks in parallel.");

  Serial.println("");
  delay(1000);

#if 1
  while (true) { delay(1000); }
#endif
}
