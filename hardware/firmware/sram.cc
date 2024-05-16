// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Firmware that runs on the microcontroller inside the cartridge.
// The microcontroller accepts commands from the player in the Sega ROM, and
// can stream video from the Internet to the cartridge's shared banks of SRAM.

// This is the SRAM interface.

#include "fast-gpio.h"

// Explicitly unrolled loop for 16 bits of data.
#define X16(a) { a; a; a; a; a; a; a; a; a; a; a; a; a; a; a; a; }
static inline void sram_set_word(uint16_t word_data) {
  X16(
    if (word_data & 0x8000) {
      FAST_SET(SRAM_PIN__DATA_NEXT_BIT);
    } else {
      FAST_CLEAR(SRAM_PIN__DATA_NEXT_BIT);
    }

    // Clock in the bit (rising edge).
    FAST_PULSE_ACTIVE_HIGH(SRAM_PIN__DATA_CLOCK);

    // Prep the next bit.
    word_data <<= 1;
  );
}

void sram_init() {
  // Set output modes on all SRAM pins.
  FAST_SET_OUTPUT_MODE(SRAM_PIN__ADDR_RESET);
  FAST_SET_OUTPUT_MODE(SRAM_PIN__ADDR_CLOCK);
  FAST_SET_OUTPUT_MODE(SRAM_PIN__DATA_NEXT_BIT);
  FAST_SET_OUTPUT_MODE(SRAM_PIN__DATA_CLOCK);
  FAST_SET_OUTPUT_MODE(SRAM_PIN__DATA_WRITE);

  // Disable active-low signals by default (setting them high).
  FAST_SET(SRAM_PIN__ADDR_RESET);
  FAST_SET(SRAM_PIN__DATA_WRITE);

  // Set other outputs low by default.
  FAST_CLEAR(SRAM_PIN__ADDR_CLOCK);
  FAST_CLEAR(SRAM_PIN__DATA_NEXT_BIT);
  FAST_CLEAR(SRAM_PIN__DATA_CLOCK);
}

void sram_write(uint16_t *data, int num_words) {
  // Reset the write address to 0.
  FAST_PULSE_ACTIVE_LOW(SRAM_PIN__ADDR_RESET);

  for (int i = 0; i < num_words; ++i) {
    sram_set_word(data[i]);

    // Write the word (active low).
    FAST_PULSE_ACTIVE_LOW(SRAM_PIN__DATA_WRITE);

    // Clock up to the next write address (rising edge).
    FAST_PULSE_ACTIVE_HIGH(SRAM_PIN__ADDR_CLOCK);
  }
}
