// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Firmware that runs on the microcontroller inside the cartridge.
// The microcontroller accepts commands from the player in the Sega ROM, and
// can stream video from the Internet to the cartridge's shared banks of SRAM.

// This is a set of basic string utilities.

#ifndef _KINETOSCOPE_STRING_UTIL_H

void copy_string(char* destination, const char* source, int size);
void concatenate_string(char* destination, const char* source, int size);

#endif // _KINETOSCOPE_STRING_UTIL_H
