// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Emulation of Kinetoscope video streaming hardware in BlastEm.

#ifndef _KINETOSCOPE_H
#define _KINETOSCOPE_H

void kinetoscope_init(void *sram_buffer, uint32_t sram_size);
void *kinetoscope_write_16(uint32_t address, void *context, uint16_t value);
void *kinetoscope_write_8(uint32_t address, void *context, uint8_t value);
uint16_t kinetoscope_read_16(uint32_t address, void *context);
uint8_t kinetoscope_read_8(uint32_t address, void *context);

#endif // _KINETOSCOPE_H
