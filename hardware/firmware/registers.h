// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Firmware that runs on the Adafruit ESP32 V2 Feather inside the cartridge.
// The feather accepts commands from the player in the Sega ROM, and can stream
// video from WiFi to the cartridge's shared banks of SRAM.

// This is the interface to the registers and sync token.  These are connected
// to the feather via an I2C port expander.  The registers are written by the
// Sega to send commands to the feather, and the sync token is a shared bit
// between the two for the Sega to notify the feather of new commands and for
// the feather to notify the Sega of a command's completion.

#ifndef _KINETOSCOPE_REGISTERS_H

#include <inttypes.h>

void registers_init();

int is_sync_token_set();

void clear_sync_token();

uint8_t read_register(int register_address);

#endif // _KINETOSCOPE_REGISTERS_H
