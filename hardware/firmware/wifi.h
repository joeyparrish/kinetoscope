// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Firmware that runs on the Adafruit ESP32 V2 Feather inside the cartridge.
// The feather accepts commands from the player in the Sega ROM, and can stream
// video from WiFi to the cartridge's shared banks of SRAM.

// This is the interface to the feather's WiFi.

#ifndef _KINETOSCOPE_WIFI_H

void wifi_init(const char* ssid, const char* password);

int wifi_https_fetch(const char* server, uint16_t port, const char* path,
                     int start_byte, uint8_t* data, int size);

#endif // _KINETOSCOPE_WIFI_H
