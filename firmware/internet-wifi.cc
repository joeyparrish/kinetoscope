// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Firmware that runs on the microcontroller inside the cartridge.
// The microcontroller accepts commands from the player in the Sega ROM, and
// can stream video from the Internet to the cartridge's shared banks of SRAM.

// This is the interface to the microcontroller's WiFi, for those with WiFi.

// These boards have WiFi.
#if defined(ARDUINO_ARCH_RP2040)

#include <HardwareSerial.h>
#include <WiFi.h>
#include <WiFiClient.h>

#include "internet.h"

static int status = WL_IDLE_STATUS;

static WiFiClient wifi_client;

Client* internet_init_wifi(const char* ssid, const char* password,
                           unsigned int timeout_seconds) {
  Serial.print("Attempting to connect to SSID: ");
  Serial.println(ssid);

  if (password && *password) {
    WiFi.begin(ssid, password);
  } else {
    WiFi.begin(ssid);
  }

  long timeout_ms = timeout_seconds * 1000L;
  long start_ms = millis();
  while (WiFi.status() != WL_CONNECTED) {
    long end_ms = millis();
    if (end_ms - start_ms >= timeout_ms) {
      Serial.println("WiFi timeout!");
      return NULL;
    }
    delay(500);
  }
  Serial.println("Connected to WiFi!");

  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  long rssi = WiFi.RSSI();
  Serial.print("Signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");

  wifi_client.setNoDelay(true);

  return &wifi_client;
}

#else

// Stub for boards without WiFi.
Client* internet_init_wifi(const char* ssid, const char* password) {
  return NULL;
}

#endif
