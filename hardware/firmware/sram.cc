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

static int leftover = -1;

// Explicitly unrolled loop for 16 bits of data.
#define X16(a) { a; a; a; a; a; a; a; a; a; a; a; a; a; a; a; a; }
static inline void sram_write_word(uint16_t word_data) {
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

  // Write the word (active low).
  FAST_PULSE_ACTIVE_LOW(SRAM_PIN__DATA_WRITE);

  // Clock up to the next write address (rising edge).
  FAST_PULSE_ACTIVE_HIGH(SRAM_PIN__ADDR_CLOCK);
}

void sram_init() {
  // Set output modes on all SRAM pins.
  pinMode(SRAM_PIN__ADDR_RESET, OUTPUT);
  pinMode(SRAM_PIN__ADDR_CLOCK, OUTPUT);
  pinMode(SRAM_PIN__DATA_NEXT_BIT, OUTPUT);
  pinMode(SRAM_PIN__DATA_CLOCK, OUTPUT);
  pinMode(SRAM_PIN__DATA_WRITE, OUTPUT);

  // Disable active-low signals by default (setting them high).
  FAST_SET(SRAM_PIN__ADDR_RESET);
  FAST_SET(SRAM_PIN__DATA_WRITE);

  // Set other outputs low by default.
  FAST_CLEAR(SRAM_PIN__ADDR_CLOCK);
  FAST_CLEAR(SRAM_PIN__DATA_NEXT_BIT);
  FAST_CLEAR(SRAM_PIN__DATA_CLOCK);

  leftover = -1;
}

void sram_start_bank(int bank) {
  // FIXME: select bank

  // Reset the write address to 0.
  FAST_PULSE_ACTIVE_LOW(SRAM_PIN__ADDR_RESET);
}

#define MAKE_WORD(high, low) ((((uint16_t)high) << 8) | (low))

void sram_write(const uint8_t *data, int num_bytes) {
  if (num_bytes == 0) {
    return;
  }

  int i = 0;

  if (leftover >= 0) {
    uint16_t word = MAKE_WORD(leftover, data[0]);
    sram_write_word(word);
    i++;
    leftover = -1;
  }

  for (; i < num_bytes - 1; i += 2) {
    uint16_t word = MAKE_WORD(data[i], data[i + 1]);
    sram_write_word(word);
  }

  if (i == num_bytes - 1) {
    leftover = data[i];
  }
}

void sram_flush() {
  if (leftover >= 0) {
    uint16_t word = MAKE_WORD(leftover, 0);
    sram_write_word(word);
  }

  leftover = -1;
}
