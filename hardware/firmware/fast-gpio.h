// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Firmware that runs on the microcontroller inside the cartridge.
// The microcontroller accepts commands from the player in the Sega ROM, and
// can stream video from the Internet to the cartridge's shared banks of SRAM.

// This is a set of macros for fast GPIO operations.
// These differ per microcontroller, and I have had to experiment with several
// microcontrollers to evaluate performance and choose the final hardware.
// These macros let me do GPIO operations more generically, but also as quickly
// as possible.  The standard Arudino library functions for GPIO are not nearly
// as fast.

#include <Arduino.h>

#if defined(ARDUINO_ARCH_RP2040)  // e.g. Raspberry Pi Pico (W)

#define SRAM_PIN__WRITE_BANK_1  12
#define SRAM_PIN__WRITE_BANK_2  13

#define SRAM_PIN__ADDR_RESET    15
#define SRAM_PIN__ADDR_CLOCK    20

#define SRAM_PIN__DATA_NEXT_BIT 21
#define SRAM_PIN__DATA_CLOCK    22
#define SRAM_PIN__DATA_WRITE    14

#define SYNC_PIN__READY         10
#define SYNC_PIN__CLEAR         11

#define REG_PIN__A0              8
#define REG_PIN__A1              9

#define REG_PIN__D0              0
#define REG_PIN__D1              1
#define REG_PIN__D2              2
#define REG_PIN__D3              3
#define REG_PIN__D4              4
#define REG_PIN__D5              5
#define REG_PIN__D6              6
#define REG_PIN__D7              7

#define REG_PIN__D_MASK    0x000000ff
#define REG_PIN__D_SHIFT            0

#define FAST_CLEAR(PIN) sio_hw->gpio_clr = 1 << (PIN)
#define FAST_SET(PIN) sio_hw->gpio_set = 1 << (PIN)
#define FAST_GET(PIN) (sio_hw->gpio_in & (1 << (PIN)))
#define FAST_READ_MULTIPLE(MASK, SHIFT) ((sio_hw->gpio_in & (MASK)) >> SHIFT)

#else

#define SRAM_PIN__WRITE_BANK_1   0
#define SRAM_PIN__WRITE_BANK_2   0

#define SRAM_PIN__ADDR_RESET     0
#define SRAM_PIN__ADDR_CLOCK     0

#define SRAM_PIN__DATA_NEXT_BIT  0
#define SRAM_PIN__DATA_CLOCK     0
#define SRAM_PIN__DATA_WRITE     0

#define SYNC_PIN__READY          0
#define SYNC_PIN__CLEAR          0

#define REG_PIN__A0              0
#define REG_PIN__A1              0

#define REG_PIN__D0              0
#define REG_PIN__D1              0
#define REG_PIN__D2              0
#define REG_PIN__D3              0
#define REG_PIN__D4              0
#define REG_PIN__D5              0
#define REG_PIN__D6              0
#define REG_PIN__D7              0

#define REG_PIN__D_MASK    0x00000000
#define REG_PIN__D_SHIFT            0

#define FAST_CLEAR(PIN) digitalWrite(PIN, LOW)
#define FAST_SET(PIN) digitalWrite(PIN, HIGH)
#define FAST_GET(PIN) digitalRead(PIN)
#define FAST_READ_MULTIPLE(MASK, SHIFT) 0

#error No fast GPIO or pin definitions for this board!

#endif


#define FAST_PULSE_ACTIVE_LOW(PIN) { \
  FAST_CLEAR(PIN); \
  FAST_SET(PIN); \
}

#define FAST_PULSE_ACTIVE_HIGH(PIN) { \
  FAST_SET(PIN); \
  FAST_CLEAR(PIN); \
}

#define FAST_WRITE(PIN, VALUE) { \
  if (VALUE) { \
    FAST_SET(PIN); \
  } else { \
    FAST_CLEAR(PIN); \
  } \
}
