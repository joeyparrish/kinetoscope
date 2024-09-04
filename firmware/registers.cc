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
  pinMode(REG_PIN__D0, INPUT_PULLDOWN);
  pinMode(REG_PIN__D1, INPUT_PULLDOWN);
  pinMode(REG_PIN__D2, INPUT_PULLDOWN);
  pinMode(REG_PIN__D3, INPUT_PULLDOWN);
  pinMode(REG_PIN__D4, INPUT_PULLDOWN);
  pinMode(REG_PIN__D5, INPUT_PULLDOWN);
  pinMode(REG_PIN__D6, INPUT_PULLDOWN);
  pinMode(REG_PIN__D7, INPUT_PULLDOWN);

  pinMode(REG_PIN__OE0, OUTPUT);
  pinMode(REG_PIN__OE1, OUTPUT);

  pinMode(SYNC_PIN__CMD_READY, INPUT_PULLDOWN);
  pinMode(SYNC_PIN__CMD_CLEAR, OUTPUT);
  pinMode(SYNC_PIN__ERR_SET, OUTPUT);
  pinMode(SYNC_PIN__ERR_FLAGGED, INPUT_PULLDOWN);

  // Disable active-low signals by default (setting them high).
  FAST_SET(SYNC_PIN__CMD_CLEAR);
  FAST_SET(SYNC_PIN__ERR_SET);
  FAST_SET(REG_PIN__OE0);
  FAST_SET(REG_PIN__OE1);

  clear_cmd();
}

int is_cmd_set() {
  return FAST_GET(SYNC_PIN__CMD_READY);
}

void clear_cmd() {
  FAST_PULSE_ACTIVE_LOW(SYNC_PIN__CMD_CLEAR);
}

void flag_error() {
  FAST_PULSE_ACTIVE_LOW(SYNC_PIN__ERR_SET);
}

int is_error_flagged() {
  return FAST_GET(SYNC_PIN__ERR_FLAGGED);
}

uint8_t read_register(int register_address) {
  // These are active-low signals.
  if (register_address == 0) {
    FAST_CLEAR(REG_PIN__OE0);
  } else if (register_address == 1) {
    FAST_CLEAR(REG_PIN__OE1);
  }

  // The data sheet for the 74AHC373 says the data will be available after at
  // most 11ns at 3.3V.  Here we wait 4 nops (should be about 8ns each).  This
  // should be more than safe, and still plenty fast.
  // https://www.ti.com/lit/gpn/sn74ahc373
  // It is worth noting that writing to SRAM and reading from the network are
  // the critical operations in terms of speed, and optimizing this is not so
  // important.  But reading it too fast is a problem, since many of these fast
  // GPIO operations are only 8ns long.
  asm("nop"); asm("nop"); asm("nop"); asm("nop");
  uint8_t data = FAST_READ_MULTIPLE(REG_PIN__D_MASK, REG_PIN__D_SHIFT);

  // Disable these active-low signals.
  FAST_SET(REG_PIN__OE0);
  FAST_SET(REG_PIN__OE1);
  return data;
}
