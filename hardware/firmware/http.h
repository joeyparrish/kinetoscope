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

void http_init(Client* network_client);

int http_fetch(const char* server, uint16_t port, const char* path,
               int start_byte, uint8_t* data, int size);

#endif // _KINETOSCOPE_HTTP_H
