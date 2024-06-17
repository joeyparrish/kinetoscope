// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Firmware that runs on the microcontroller inside the cartridge.
// The microcontroller accepts commands from the player in the Sega ROM, and
// can stream video from the Internet to the cartridge's shared banks of SRAM.

// This is the interface to the registers and sync token.  These are connected
// to the microcontroller via an I2C port expander.  The registers are written
// by the Sega to send commands to the microcontroller, and the sync token is a
// shared bit between the two for the Sega to notify the microcontroller of new
// commands and for the microcontroller to notify the Sega of a command's
// completion.

#ifndef _KINETOSCOPE_REGISTERS_H

#include <inttypes.h>

// These are the sega address bits A2 and A1.  There is no A0 signal from the
// sega, since it has a 16-bit data bus.
#define KINETOSCOPE_REG_CMD      0   // Sega's 0xA13000
#define KINETOSCOPE_REG_ARG      1   // Sega's 0xA13002
// These exist in hardware but aren't used at the moment.
#define KINETOSCOPE_REG_UNUSED_2 2
#define KINETOSCOPE_REG_UNUSED_3 3

#define KINETOSCOPE_CMD_ECHO        0x00  // Writes arg to SRAM
#define KINETOSCOPE_CMD_LIST_VIDEOS 0x01  // Writes video list to SRAM
#define KINETOSCOPE_CMD_START_VIDEO 0x02  // Begins streaming to SRAM
#define KINETOSCOPE_CMD_STOP_VIDEO  0x03  // Stops streaming
#define KINETOSCOPE_CMD_FLIP_REGION 0x04  // Switch SRAM banks for streaming

void registers_init();

int is_cmd_set();

void clear_cmd();

void flag_error();

int is_error_flagged();

uint8_t read_register(int register_address);

#endif // _KINETOSCOPE_REGISTERS_H
