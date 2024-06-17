// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Error reporting.

#ifndef _KINETOSCOPE_ERROR_H
#define _KINETOSCOPE_ERROR_H

// Store an error message and flag the Sega that we encountered an error.
void report_error(const char* format, ...);

// Write the stored error message to SRAM bank 0.
void write_error_to_sram();

#endif // _KINETOSCOPE_ERROR_H
