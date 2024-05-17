// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Firmware that runs on the microcontroller inside the cartridge.
// The microcontroller accepts commands from the player in the Sega ROM, and
// can stream video from the Internet to the cartridge's shared banks of SRAM.

// This is the SRAM interface.

#ifndef _KINETOSCOPE_SRAM_H

void sram_init();
void sram_start_bank(int bank);
void sram_write(const uint8_t *data, int num_bytes);
void sram_flush();

#endif // _KINETOSCOPE_SRAM_H
