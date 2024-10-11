// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Shared SRAM code.
// Beware: The Sega runs a version of sram_march_test, and "int" in that
// environment is only 15 bits (plus sign bit).  So use specific uint* types
// instead!

#define SRAM_BANK_SIZE_BYTES (1 << 20)

/**
 * Test patterns that the firmware or emulator can write and the test ROM can
 * read.  Requires these macros (with sample definitions):
 *
 * #define SRAM_MARCH_TEST_START(bank) sram_start_bank(bank)
 * #define SRAM_MARCH_TEST_DATA(offset, data) sram_write(&data, 1)
 * #define SRAM_MARCH_TEST_END() sram_flush_and_release_bank()
 */
bool sram_march_test(int pass) {
  int bank = pass & 1;
  SRAM_MARCH_TEST_START(bank);

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
    case 15: {
      uint32_t start_offset = pass / 2;
      for (uint32_t offset = 0; offset < SRAM_BANK_SIZE_BYTES; ++offset) {
        uint32_t bit = (offset + start_offset) % 8;
        uint8_t data = 1U << bit;
        SRAM_MARCH_TEST_DATA(offset, data);
      }
      break;
    }

    // Write the lowest 8 bits of the address to each byte of SRAM.
    case 16:
    case 17:
      for (uint32_t offset = 0; offset < SRAM_BANK_SIZE_BYTES; ++offset) {
        uint8_t data = offset & 0xff;
        SRAM_MARCH_TEST_DATA(offset, data);
      }
      break;

    // Write the lowest 8 bits of the address (inverted) to each byte of SRAM.
    case 18:
    case 19:
      for (uint32_t offset = 0; offset < SRAM_BANK_SIZE_BYTES; ++offset) {
        uint8_t data = (offset & 0xff) ^ 0xff;
        SRAM_MARCH_TEST_DATA(offset, data);
      }
      break;

    // Write repeating sequences with prime periods, to avoid any periodic
    // repeating on address bit boundaries (powers of 2).  To make bank 1
    // different from bank 0, start counter at non-zero for bank 1.
    case 20:
    case 21: {
      uint32_t primes[8] = { 251, 241, 239, 233, 229, 227, 223, 211 };
      uint32_t prime_index = 0;
      uint32_t offset = 0;
      uint32_t counter = bank * 199;
      for (; offset < SRAM_BANK_SIZE_BYTES; ++offset, ++counter) {
        if (counter == primes[prime_index] * 255) {
          prime_index = (prime_index + 1) % 8;
          counter = 0;
        }
        uint8_t data = counter % primes[prime_index];
        SRAM_MARCH_TEST_DATA(offset, data);
      }
      break;
    }
  }

  SRAM_MARCH_TEST_END();
  return true;
}

#define SRAM_MARCH_TEST_NUM_PASSES 22
