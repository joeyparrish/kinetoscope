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

// An actual MAC address assigned to me with my ethernet board.
// Don't put two of these devices on the same network, y'all.
uint8_t MAC_ADDR[] = { 0x98, 0x76, 0xB6, 0x12, 0xD4, 0x9E };

static void freeze() {
  while (true) { delay(1000); }
}

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

void loop() {
#if 0
  if (!is_error_flagged()) {
    run_tests();
  }
#endif

  freeze();
}
