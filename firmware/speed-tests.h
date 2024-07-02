// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Firmware that runs on the microcontroller inside the cartridge.
// The microcontroller accepts commands from the player in the Sega ROM, and
// can stream video from the Internet to the cartridge's shared banks of SRAM.

// Microcontroller function speed tests.

#ifndef _KINETOSCOPE_SPEED_TESTS_H

// Prints results to serial.
void run_tests();

#endif // _KINETOSCOPE_SPEED_TESTS_H
