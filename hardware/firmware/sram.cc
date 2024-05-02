// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Firmware that runs on the Adafruit ESP32 V2 Feather inside the cartridge.
// The feather accepts commands from the player in the Sega ROM, and can stream
// video from WiFi to the cartridge's shared banks of SRAM.

// This is the SRAM interface.

#include <Arduino.h>

#include "sram.h"

// The register to set or clear address bits (pins 32+)
#define SRAM_ADDR_REG(S_OR_C) GPIO_OUT1_W1T##S_OR_C##_REG
// The register to set output mode for address bits (pins 32+)
#define SRAM_ADDR_OUTPUT      GPIO_ENABLE1_W1TS_REG
// Pin 33, active low
#define SRAM_ADDR_RESET    (1 << (33-32))
// Pin 32, rising edge
#define SRAM_ADDR_CLOCK    (1 << (32-32))

// The register to set or clear data bits (pins 0-31)
#define SRAM_DATA_REG(S_OR_C) GPIO_OUT_W1T##S_OR_C##_REG
// The register to set output mode for data bits (pins 0-31)
#define SRAM_DATA_OUTPUT      GPIO_ENABLE_W1TS_REG
// Pin 5
#define SRAM_DATA_NEXT_BIT (1 << 5)
// Pin 19, rising edge
#define SRAM_DATA_CLOCK    (1 << 19)
// Pin 21, active low
#define SRAM_DATA_WRITE    (1 << 21)

// Relatively readable macros to set GPIO modes.
#define OUTPUT_MODE(REG, BITS) REG_WRITE(REG, BITS)

// Relatively readable macros to set GPIO bits very very quickly.
// Each call takes about 50ns.
#define SET(REG, BITS)   REG_WRITE(REG(S), BITS)
#define CLEAR(REG, BITS) REG_WRITE(REG(C), BITS)

static inline void sram_set_pin_modes() {
  // Set output modes on all SRAM pins.
  OUTPUT_MODE(SRAM_ADDR_OUTPUT,
      SRAM_ADDR_RESET | SRAM_ADDR_CLOCK);
  OUTPUT_MODE(SRAM_DATA_OUTPUT,
      SRAM_DATA_NEXT_BIT | SRAM_DATA_CLOCK | SRAM_DATA_WRITE);

  // Disable active-low signals by default (setting them high).
  SET(SRAM_ADDR_REG, SRAM_ADDR_RESET);
  SET(SRAM_DATA_REG, SRAM_DATA_WRITE);

  // Set other outputs low by default.
  CLEAR(SRAM_ADDR_REG, SRAM_ADDR_CLOCK);
  CLEAR(SRAM_DATA_REG, SRAM_DATA_NEXT_BIT | SRAM_DATA_CLOCK);
}

// Hold time for any pin we activate is naturally about 50ns
// without any explicit delays. We can't write any faster than that.

static inline void sram_reset_address() {
  // Reset the write address to 0 (active low).
  CLEAR(SRAM_ADDR_REG, SRAM_ADDR_RESET);
  SET(SRAM_ADDR_REG, SRAM_ADDR_RESET);
}

static inline void sram_next_address() {
  /// Clock up to the next write address (rising edge).
  SET(SRAM_ADDR_REG, SRAM_ADDR_CLOCK);
  CLEAR(SRAM_ADDR_REG, SRAM_ADDR_CLOCK);
}

// Explicitly unrolled loop for 16 bits of data.
#define X16(a) { a; a; a; a; a; a; a; a; a; a; a; a; a; a; a; a; }
static inline void sram_set_word(uint16_t word_data) {
  // ~1200ms to write ~3s of audio+video data (~450k words).
  X16(
    if (word_data & 1) {
      SET(SRAM_DATA_REG, SRAM_DATA_NEXT_BIT);
    } else {
      CLEAR(SRAM_DATA_REG, SRAM_DATA_NEXT_BIT);
    }

    // Clock in the bit (rising edge).
    SET(SRAM_DATA_REG, SRAM_DATA_CLOCK);
    CLEAR(SRAM_DATA_REG, SRAM_DATA_CLOCK);

    // Prep the next bit.
    word_data >>= 1;
  );

  // Write the word (active low).
  CLEAR(SRAM_DATA_REG, SRAM_DATA_WRITE);
  SET(SRAM_DATA_REG, SRAM_DATA_WRITE);
}

void sram_init() {
  sram_set_pin_modes();
}

void sram_write(uint16_t *data, int num_words) {
  sram_reset_address();

  for (int i = 0; i < num_words; ++i) {
    sram_set_word(data[i]);
    sram_next_address();
  }
}
