// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Firmware that runs on the microcontroller inside the cartridge.
// The microcontroller accepts commands from the player in the Sega ROM, and
// can stream video from the Internet to the cartridge's shared banks of SRAM.

// Error reporting.

#ifndef _KINETOSCOPE_ERROR_H
#define _KINETOSCOPE_ERROR_H

// Store an error message and flag the Sega that we encountered an error.
void report_error(const char* format, ...);

// Write the stored error message to SRAM bank 0.
void write_error_to_sram();

#endif // _KINETOSCOPE_ERROR_H
