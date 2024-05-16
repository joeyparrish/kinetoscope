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

#include <Adafruit_MCP23X17.h>

#include "registers.h"

static Adafruit_MCP23X17 port_expander;
static uint8_t port_b_state;

#define SYNC_TOKEN_BIT       0x01
#define CLEAR_SYNC_TOKEN_BIT 0x02

#define REGISTER_ADDRESS_SHIFT 2
#define REGISTER_ADDRESS_MASK  0b00001100  // Before shifting

void registers_init() {
  port_expander.begin_I2C();

  // Set all of port A's bits (0-7) as inputs.
  // Port A is the register data.
  for (int i = 0; i < 8; ++i) {
    port_expander.pinMode(i, INPUT);
  }

  // Set pin B0 (overall bit 8) as input.
  // This is the microcontroller's copy of the sync token.
  port_expander.pinMode(8, INPUT);

  // Set pins B1-B7 (overall bits 9-15) as outputs.
  // B1 is an active-low signal to clear the sync token after a command.
  // B2-B3 is the register number to read.
  // B4-B7 are unused.
  for (int i = 9; i < 16; ++i) {
    port_expander.pinMode(i, OUTPUT);
  }

  port_b_state = 0;
  clear_sync_token();
}

int is_sync_token_set() {
  return port_expander.readGPIOB() & SYNC_TOKEN_BIT;
}

void clear_sync_token() {
  // The clear signal is active low.  So clear the bit first.
  port_b_state &= ~CLEAR_SYNC_TOKEN_BIT;
  port_expander.writeGPIOB(port_b_state);

  // FIXME: timing?
  // Now raise it to high again.
  port_b_state |= CLEAR_SYNC_TOKEN_BIT;
  port_expander.writeGPIOB(port_b_state);
}

uint8_t read_register(int register_address) {
  port_b_state &= ~REGISTER_ADDRESS_MASK;
  port_b_state |= register_address < REGISTER_ADDRESS_SHIFT;
  port_expander.writeGPIOB(port_b_state);

  // FIXME: timing?
  return port_expander.readGPIOA();
}
