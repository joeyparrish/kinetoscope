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
#include "sram.h"

static int leftover = -1;
static int active_bank_pin = -1;

#define SRAM_BANK_SIZE_BYTES (1 << 20)

// Explicitly unrolled loop for 16 bits of data.
#define X16(a) { a; a; a; a; a; a; a; a; a; a; a; a; a; a; a; a; }
static inline void sram_write_word(uint16_t word_data) {
  // ~20ns setup time from next data bit to rising edge of clock
  X16(
    FAST_WRITE(SRAM_PIN__DATA_NEXT_BIT, word_data & 0x8000);

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
  pinMode(SRAM_PIN__WRITE_BANK_1, OUTPUT);
  pinMode(SRAM_PIN__WRITE_BANK_2, OUTPUT);
  pinMode(SRAM_PIN__ADDR_RESET, OUTPUT);
  pinMode(SRAM_PIN__ADDR_CLOCK, OUTPUT);
  pinMode(SRAM_PIN__DATA_NEXT_BIT, OUTPUT);
  pinMode(SRAM_PIN__DATA_CLOCK, OUTPUT);
  pinMode(SRAM_PIN__DATA_WRITE, OUTPUT);

  // Disable active-low signals by default (setting them high).
  FAST_SET(SRAM_PIN__ADDR_RESET);
  FAST_SET(SRAM_PIN__DATA_WRITE);

  // Set other outputs low by default.
  FAST_CLEAR(SRAM_PIN__WRITE_BANK_1);
  FAST_CLEAR(SRAM_PIN__WRITE_BANK_2);
  FAST_CLEAR(SRAM_PIN__ADDR_CLOCK);
  FAST_CLEAR(SRAM_PIN__DATA_NEXT_BIT);
  FAST_CLEAR(SRAM_PIN__DATA_CLOCK);

  leftover = -1;
}

void sram_start_bank(int bank) {
  sram_flush_and_release_bank();

  active_bank_pin = bank ? SRAM_PIN__WRITE_BANK_2 : SRAM_PIN__WRITE_BANK_1;
  FAST_SET(active_bank_pin);

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

void sram_flush_and_release_bank() {
  if (active_bank_pin >= 0) {
    if (leftover >= 0) {
      uint16_t word = MAKE_WORD(leftover, 0);
      sram_write_word(word);
    }

    FAST_CLEAR(active_bank_pin);
    active_bank_pin = -1;
  }

  leftover = -1;
}

// Write a test pattern that the Sega can read and verify.
void sram_march_test(int pass) {
  sram_start_bank(pass & 1);

  switch (pass) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
      for (int i = 0; i < SRAM_BANK_SIZE_BYTES; ++i) {
        int bit = (i + pass / 2) % 8;
        uint8_t data = 1 << bit;
        sram_write(&data, 1);
      }
      break;

    case 16:
    case 17:
      for (int i = 0; i < SRAM_BANK_SIZE_BYTES; ++i) {
        uint8_t data = i & 0xff;
        sram_write(&data, 1);
      }
      break;

    case 18:
    case 19:
      for (int i = 0; i < SRAM_BANK_SIZE_BYTES; ++i) {
        uint8_t data = (i & 0xff) ^ 0xff;
        sram_write(&data, 1);
      }
      break;
  }

  sram_flush_and_release_bank();
}
