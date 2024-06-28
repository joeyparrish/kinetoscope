// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Firmware that runs on the microcontroller inside the cartridge.
// The microcontroller accepts commands from the player in the Sega ROM, and
// can stream video from the Internet to the cartridge's shared banks of SRAM.

// This is the interface to the network.

#ifndef _KINETOSCOPE_INTERNET_H

Client* internet_init_wifi(const char* ssid, const char* password);

Client* internet_init_wired(uint8_t* mac);

#endif // _KINETOSCOPE_INTERNET_H
