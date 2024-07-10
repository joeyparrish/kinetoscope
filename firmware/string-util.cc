// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Firmware that runs on the microcontroller inside the cartridge.
// The microcontroller accepts commands from the player in the Sega ROM, and
// can stream video from the Internet to the cartridge's shared banks of SRAM.

// This is a set of basic string utilities.

#include <string.h>

#include "string-util.h"

void copy_string(char* destination, const char* source, int size) {
  // strncpy manual: "No null-character is implicitly appended at the end of
  // destination if source is longer than num."
  strncpy(destination, source, size);
  destination[size - 1] = '\0';
}

void concatenate_string(char* destination, const char* source, int size) {
  size_t len = strlen(destination);
  strncpy(destination + len, source, size - len);
  destination[size - 1] = '\0';
}
