// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// ROM header for the project.

#include <genesis.h>

__attribute__((externally_visible))
const ROMHeader rom_header = {
  // Used by emulators to decide what special hardware to emulate.
  // Though you may not find emulation of this hardware anywhere that I didn't
  // contribute it.  See the folder "emulator-patches" to modify your OSS Sega
  // emulator.
  "SEGA VIDEOSTREAM",
  // Copyright line.
  "(C) Joey Parrish",
  // Game title.
  "Kinetoscope Streaming                           ",
  // Localized game title.
  "Kinetoscope Streaming                           ",
  // Serial number. GM prefix means "game". The rest is meaningless.
  "GM 04390116-42",
  // ROM checksum.
  0x0000,
  // Device support.  "J" means 3-button controller.
  "J               ",
  // Cartridge ROM/RAM address range.
  0x00000000,
  0x003FFFFF,
  // RAM address range.
  0xE0FF0000,
  0xE0FFFFFF,
  // Declare SRAM.
  "RA",
  // A0 = 16-bit SRAM, 20 = reserved.
  0xA020,
  // SRAM address range.
  0x00200000,
  0x003FFFFF,
  // No modem support.
  "            ",
  // Reserved, just spaces.
  "                                        ",
  // Region support: Japan, US, Europe.
  "JUE             "
};
