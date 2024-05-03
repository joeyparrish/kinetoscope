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

#define SERVER "cdn.syndication.twimg.com"
#define PATH   "/widgets/followbutton/info.json?screen_names=adafruit"

static void test_sram_speed() {
  // 450966 words = ~3s worth of audio+video data
  // with headers and worst-case padding.
  int data_size = 450966;

  uint16_t* data = (uint16_t*)ps_malloc(data_size * sizeof(uint16_t));

  // Takes about 1200ms total, or about 2660ns per word.
  long start = millis();
  sram_write(data, data_size);
  long end = millis();

  free(data);

  Serial.print(end - start);
  Serial.println(" ms total to write 3s of data to SRAM\n");
}

static void test_register_speed() {
  long start = millis();
  int count = 0;
  // Takes about 470us each.
  for (int i = 0; i < 1000; ++i) {
    count += is_sync_token_set();
  }
  long end = millis();

  Serial.print(end - start);
  Serial.println(" ms total for 1000 token reads\n");

  start = millis();
  // Takes about 700us each.
  for (int i = 0; i < 1000; ++i) {
    clear_sync_token();
  }
  end = millis();

  Serial.print(end - start);
  Serial.println(" ms total for 1000 token clears\n");

  start = millis();
  // Takes about 820us each.
  for (int i = 0; i < 1000; ++i) {
    count += read_register(i & 3);
  }
  end = millis();

  Serial.print(end - start);
  Serial.println(" ms total for 1000 register reads\n");
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}  // Wait for serial port to connect

  psramInit();

  sram_init();

  registers_init();

  wifi_init(SECRET_WIFI_SSID, SECRET_WIFI_PASS);

  pinMode(LED_BUILTIN, OUTPUT);

  wifi_connect(SERVER, 443, PATH);
}

void loop() {
  test_sram_speed();

  test_register_speed();

  while (wifi_read()) {
    delay(100);
  }

  while (true) {
    delay(1000);
  }
}
