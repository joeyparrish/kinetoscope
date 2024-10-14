// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Emulation of Kinetoscope video streaming hardware.

#ifndef _KINETOSCOPE_H
#define _KINETOSCOPE_H

// Returns the address of the buffer that emulates the SRAM banks (2MB).
void* kinetoscope_init();
void *kinetoscope_write_16(uint32_t address, void *context, uint16_t value);
void *kinetoscope_write_8(uint32_t address, void *context, uint8_t value);
uint16_t kinetoscope_read_16(uint32_t address, void *context);
uint8_t kinetoscope_read_8(uint32_t address, void *context);

#endif // _KINETOSCOPE_H
