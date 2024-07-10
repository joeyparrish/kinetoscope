// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Firmware that runs on the microcontroller inside the cartridge.
// The microcontroller accepts commands from the player in the Sega ROM, and
// can stream video from the Internet to the cartridge's shared banks of SRAM.

// This is the interface to HTTP requests.

#ifndef _KINETOSCOPE_HTTP_H

#include <Client.h>

typedef bool (*http_data_callback)(const uint8_t* buffer, int bytes);

void http_init(Client* network_client);

// Reports error messages through error.h and returns -1 on failure
int http_fetch(const char* server, uint16_t port, const char* path,
               int start_byte, int size, http_data_callback callback);

#endif // _KINETOSCOPE_HTTP_H
