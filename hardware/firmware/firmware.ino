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
#include "speed-tests.h"
#include "sram.h"

// An actual MAC address assigned to me with my ethernet board.
// Don't put two of these devices on the same network, y'all.
uint8_t MAC_ADDR[] = { 0x98, 0x76, 0xB6, 0x12, 0xD4, 0x9E };

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }  // Wait for serial port to connect

  // Delay startup so we can have the serial monitor attached.
  delay(1000);
  Serial.println("Kinetoscope boot!\n");

  sram_init();
  registers_init();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // Prefer wired, fall back to WiFi if configured.
  Client* client = internet_init_wired(MAC_ADDR);

  if (!client) {
    Serial.println("Wired connection failed!");

#if defined(ARDUINO_ARCH_RP2040)
    if (strlen(SECRET_WIFI_SSID) && strlen(SECRET_WIFI_PASS)) {
      client = internet_init_wifi(SECRET_WIFI_SSID, SECRET_WIFI_PASS);
      if (!client) {
        Serial.println("WiFi connection failed!");
      }
    } else {
      Serial.println("WiFi not configured!");
    }
#else
    Serial.println("WiFi hardware not available!");
#endif
  }

  // TODO: Report status back to the Sega.
  if (!client) {
    Serial.println("Failed to connect to the network!");
    while (true) { delay(1000); }
  }

  http_init(client);
}

void loop() {
  run_tests();
  while (true) { delay(1000); }
}
