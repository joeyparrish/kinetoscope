// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Shared SRAM code.

#define SRAM_BANK_SIZE_BYTES (1 << 20)

/**
 * Test patterns that the firmware or emulator can write and the test ROM can
 * read.  Requires these macros (with sample definitions):
 *
 * #define SRAM_MARCH_TEST_START(bank) sram_start_bank(bank)
 * #define SRAM_MARCH_TEST_DATA(data) sram_write(&data, 1)
 * #define SRAM_MARCH_TEST_END() sram_flush_and_release_bank()
 */
bool sram_march_test(int pass) {
  SRAM_MARCH_TEST_START(pass & 1);

  switch (pass) {
    // Bit-sliding test.
    // Pass 0:  01 02 04 08 10 20 40 80 ...
    // Pass 1:  Same as pass 0, but on bank 1 instead of 0
    // Pass 2:  02 04 08 10 20 40 80 01 ...
    // Pass 4:  04 08 10 20 40 80 01 02 ...
    // Pass 6:  08 10 20 40 80 01 02 04 ...
    // Pass 8:  10 20 40 80 01 02 04 08 ...
    // Pass 10: 20 40 80 01 02 04 08 10 ...
    // Pass 12: 40 80 01 02 04 08 10 20 ...
    // Pass 14: 80 01 02 04 08 10 20 40 ...
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
        SRAM_MARCH_TEST_DATA(data);
      }
      break;

    // Write the lowest 8 bits of the address to each byte of SRAM.
    case 16:
    case 17:
      for (int i = 0; i < SRAM_BANK_SIZE_BYTES; ++i) {
        uint8_t data = i & 0xff;
        SRAM_MARCH_TEST_DATA(data);
      }
      break;

    // Write the lowest 8 bits of the address (inverted) to each byte of SRAM.
    case 18:
    case 19:
      for (int i = 0; i < SRAM_BANK_SIZE_BYTES; ++i) {
        uint8_t data = (i & 0xff) ^ 0xff;
        SRAM_MARCH_TEST_DATA(data);
      }
      break;
  }

  SRAM_MARCH_TEST_END();
  return true;
}