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

// For debugging, we can slow down GPIO.
//#define GO_SLOW

#if defined(ARDUINO_ARCH_RP2040)  // e.g. Raspberry Pi Pico (W)

#define SRAM_PIN__WRITE_BANK_0  12
#define SRAM_PIN__WRITE_BANK_1  13

#define SRAM_PIN__ADDR_RESET    15
#define SRAM_PIN__ADDR_CLOCK    20

#define SRAM_PIN__DATA_NEXT_BIT 21
#define SRAM_PIN__DATA_CLOCK    22
#define SRAM_PIN__DATA_WRITE    14

#define SYNC_PIN__CMD_READY     10
#define SYNC_PIN__CMD_CLEAR     11
#define SYNC_PIN__ERR_FLAGGED   27
#define SYNC_PIN__ERR_SET       26

#define REG_PIN__OE0             8
#define REG_PIN__OE1             9

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

// Without the nops, pulses happen in about 16ns, but the voltage change
// from the GPIO pins (~8ns/V over 3.3V) is much slower than the CPU speed
// (125MHz or 8ns/cycle).  So without nops, at top speed, a signal would
// bounce between ~2V and ~1V, never reaching high (>= 2.3V) or low (<= 0.8V).

// One nop after each pin change adds roughly doubles the duration to 32ns.
// With this timing, the voltage ranges from ~2.4V to ~0.5V.  It spends about
// 4ns in the "high" zone and about 6ns in the "low" zone.  Our minimum pulse
// widths for most ICs are around 7ns (74AHC74 clear/set, 74LV163A clock,
// 74LV164 clock), so this is not long enough in stable states.

// With two nops, the pin tends to stay in the right range for 12-16ns,
// reaching all the way to VCC and 0V.
#define FAST_GPIO_DELAY() { asm("nop"); asm("nop"); }

// SRAM write-enable pulses must be at least 45ns, so an extra delay is needed.
// Each nop adds about 8ns.  The standard delay above already gives us ~16ns.
// So we need 4-5 more.
#define SRAM_GPIO_DELAY() { \
  asm("nop"); asm("nop"); asm("nop"); asm("nop"); asm("nop"); \
}

#ifdef GO_SLOW
# define FAST_CLEAR(PIN) digitalWrite(PIN, LOW);
# define FAST_SET(PIN) digitalWrite(PIN, HIGH);
#else
# define FAST_CLEAR(PIN) sio_hw->gpio_clr = 1 << (PIN)
# define FAST_SET(PIN) sio_hw->gpio_set = 1 << (PIN)
#endif

#define FAST_GET(PIN) (sio_hw->gpio_in & (1 << (PIN)))
#define FAST_READ_MULTIPLE(MASK, SHIFT) ((sio_hw->gpio_in & (MASK)) >> SHIFT)

#else

#define SRAM_PIN__WRITE_BANK_0   0
#define SRAM_PIN__WRITE_BANK_1   0

#define SRAM_PIN__ADDR_RESET     0
#define SRAM_PIN__ADDR_CLOCK     0

#define SRAM_PIN__DATA_NEXT_BIT  0
#define SRAM_PIN__DATA_CLOCK     0
#define SRAM_PIN__DATA_WRITE     0

#define SYNC_PIN__CMD_READY      0
#define SYNC_PIN__CMD_CLEAR      0
#define SYNC_PIN__ERR_FLAGGED    0
#define SYNC_PIN__ERR_SET        0

#define REG_PIN__OE0             0
#define REG_PIN__OE1             0

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

#define SRAM_GPIO_DELAY() {}

#error No fast GPIO or pin definitions for this board!

#endif

#define FAST_PULSE_ACTIVE_LOW(PIN) { \
  FAST_CLEAR(PIN); \
  FAST_GPIO_DELAY(); \
  FAST_SET(PIN); \
}

#define FAST_PULSE_ACTIVE_HIGH(PIN) { \
  FAST_SET(PIN); \
  FAST_GPIO_DELAY(); \
  FAST_CLEAR(PIN); \
}

#define FAST_WRITE(PIN, VALUE) { \
  if (VALUE) { \
    FAST_SET(PIN); \
  } else { \
    FAST_CLEAR(PIN); \
  } \
}

// Like "fast" pulse, but a little extra delay for SRAM.
#define SRAM_PULSE_ACTIVE_LOW(PIN) { \
  FAST_CLEAR(PIN); \
  SRAM_GPIO_DELAY(); \
  FAST_SET(PIN); \
}
