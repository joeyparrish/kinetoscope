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

#include "registers.h"

#include "fast-gpio.h"

void registers_init() {
  // Set modes on register and sync pins.
  pinMode(REG_PIN__D0, INPUT);
  pinMode(REG_PIN__D1, INPUT);
  pinMode(REG_PIN__D2, INPUT);
  pinMode(REG_PIN__D3, INPUT);
  pinMode(REG_PIN__D4, INPUT);
  pinMode(REG_PIN__D5, INPUT);
  pinMode(REG_PIN__D6, INPUT);
  pinMode(REG_PIN__D7, INPUT);

  pinMode(REG_PIN__A0, OUTPUT);
  pinMode(REG_PIN__A1, OUTPUT);

  pinMode(SYNC_PIN__READY, INPUT);
  pinMode(SYNC_PIN__CLEAR, OUTPUT);

  // Disable active-low signals by default (setting them high).
  FAST_SET(SYNC_PIN__CLEAR);

  // Set other outputs low by default.
  FAST_CLEAR(REG_PIN__A0);
  FAST_CLEAR(REG_PIN__A1);

  clear_sync_token();
}

int is_sync_token_set() {
  return FAST_GET(SYNC_PIN__READY);
}

void clear_sync_token() {
  FAST_PULSE_ACTIVE_LOW(SYNC_PIN__CLEAR);
}

uint8_t read_register(int register_address) {
  FAST_WRITE(REG_PIN__A0, register_address & 1);
  FAST_WRITE(REG_PIN__A1, register_address & 2);

  // FIXME: need to delay here?
  return FAST_READ_MULTIPLE(REG_PIN__D_MASK, REG_PIN__D_SHIFT);
}
