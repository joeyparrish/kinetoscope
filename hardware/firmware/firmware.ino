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

#include "sram.h"

void test_sram_speed() {
  // 450966 words = ~3s worth of audio+video data
  // with headers and worst-case padding.
  int data_size = 450966;

  uint16_t* data = (uint16_t*)ps_malloc(data_size * sizeof(uint16_t));

  long start = millis();
  sram_write(data, data_size);
  long end = millis();

  free(data);

  Serial.print(end - start);
  Serial.println(" ms total\n");
}

void setup() {
  Serial.begin(115200);
  psramInit();
  sram_init();
}

void loop() {
  test_sram_speed();
}
