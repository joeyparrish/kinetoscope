// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Firmware that runs on the microcontroller inside the cartridge.
// The microcontroller accepts commands from the player in the Sega ROM, and
// can stream video from the Internet to the cartridge's shared banks of SRAM.

// This is the interface to the microcontroller's WiFi, for those with WiFi.

#if defined(ARDUINO_ARCH_ESP32)  // This board has WiFi.

#include <HardwareSerial.h>
#include <WiFi.h>
#include <WiFiClient.h>

#include "internet.h"

static int status = WL_IDLE_STATUS;

static WiFiClient wifi_client;

Client* internet_init_wifi(const char* ssid, const char* password) {
  Serial.print("Attempting to connect to SSID: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  // FIXME: Handle timeouts here
  while (WiFi.status() != WL_CONNECTED) {
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

#endif