// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Error reporting.

#include <Arduino.h>

#include <cstdarg>
#include <cstdio>

#include "error.h"
#include "registers.h"
#include "sram.h"

#define MAX_ERROR 256

// Stores an error message that the Sega can query later.
char error_buffer[MAX_ERROR];

void report_error(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vsnprintf(error_buffer, MAX_ERROR, format, args);
  va_end(args);
 
  Serial.print("Error reported: ");
  Serial.println(error_buffer);

  // Set a flag the Sega should notice and query later.
  flag_error();
}

void write_error_to_sram() {
  sram_start_bank(0);
  sram_write((const uint8_t *)error_buffer, MAX_ERROR);
  sram_flush_and_release_bank();
}
