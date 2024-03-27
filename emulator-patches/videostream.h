// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Emulation of video streaming hardware in BlastEm.

#ifndef _VIDEOSTREAM_H
#define _VIDEOSTREAM_H

void videostream_init(void *sram_buffer, uint32_t sram_size);
void *videostream_write_16(uint32_t address, void *context, uint16_t value);
void *videostream_write_8(uint32_t address, void *context, uint8_t value);
uint16_t videostream_read_16(uint32_t address, void *context);
uint8_t videostream_read_8(uint32_t address, void *context);

#endif // _VIDEOSTREAM_H
