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

#if defined(ARDUINO_ARCH_ESP32)  // e.g. Adafruit ESP32 Feather v2

#define SRAM_PIN__ADDR_RESET    33
#define SRAM_PIN__ADDR_CLOCK    32

#define SRAM_PIN__DATA_NEXT_BIT  5
#define SRAM_PIN__DATA_CLOCK    19
#define SRAM_PIN__DATA_WRITE    21

// Example:
// MAKE_ESP32_REG(ENABLE, 1, S) => GPIO_ENABLE1_W1TS_REG
// MAKE_ESP32_REG(OUT, , C) => GPIO_OUT_W1TC_REG
#define MAKE_ESP32_REG(TYPE, REG, S_OR_C) \
  GPIO_##TYPE####REG##_W1T##S_OR_C##_REG

#define FAST_CLEAR(PIN) { \
  if ((PIN) > 31) { \
    REG_WRITE(MAKE_ESP32_REG(OUT, 1, C), 1 << (32 - PIN)); \
  } else { \
    REG_WRITE(MAKE_ESP32_REG(OUT, , C), 1 << (PIN)); \
  } \
}

#define FAST_SET(PIN) { \
  if ((PIN) > 31) { \
    REG_WRITE(MAKE_ESP32_REG(OUT, 1, S), 1 << (32 - PIN)); \
  } else { \
    REG_WRITE(MAKE_ESP32_REG(OUT, , S), 1 << (PIN)); \
  } \
}


#elif defined(ARDUINO_ARCH_SAMD)  // e.g. Adafruit M4 boards

#define SRAM_REG                PORTA
#define SRAM_PIN__ADDR_RESET    15  // D5
#define SRAM_PIN__ADDR_CLOCK    18  // D7

#define SRAM_PIN__DATA_NEXT_BIT 19  // D9
#define SRAM_PIN__DATA_CLOCK    20  // D10
#define SRAM_PIN__DATA_WRITE    21  // D11

#define FAST_CLEAR(PIN) \
  PORT->Group[SRAM_REG].OUTCLR.reg = 1 << (PIN);

#define FAST_SET(PIN) \
  PORT->Group[SRAM_REG].OUTSET.reg = 1 << (PIN);


#elif defined(ARDUINO_ARCH_RP2040)  // e.g. Raspberry Pi Pico (W)

#define SRAM_PIN__ADDR_RESET    15
#define SRAM_PIN__ADDR_CLOCK    14

#define SRAM_PIN__DATA_NEXT_BIT 13
#define SRAM_PIN__DATA_CLOCK    12
#define SRAM_PIN__DATA_WRITE    11

#define FAST_CLEAR(PIN) sio_hw->gpio_clr = 1 << (PIN)
#define FAST_SET(PIN) sio_hw->gpio_set = 1 << (PIN)


#else

#define SRAM_PIN__ADDR_RESET     0
#define SRAM_PIN__ADDR_CLOCK     0

#define SRAM_PIN__DATA_NEXT_BIT  0
#define SRAM_PIN__DATA_CLOCK     0
#define SRAM_PIN__DATA_WRITE     0

#define FAST_CLEAR(PIN) digitalWrite(PIN, LOW)
#define FAST_SET(PIN) digitalWrite(PIN, HIGH)

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
