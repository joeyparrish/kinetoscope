// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Sega video state.
//
// This can run on the Sega, inside an emulator, or in the firmware of the
// streaming hardware.

#ifndef _SEGAVIDEO_STATE_H
#define _SEGAVIDEO_STATE_H

typedef enum {
  Idle,
  Setup,
  Menu,
  Player,
  Error,
} SegaVideoState;

SegaVideoState segavideo_getState();

#endif // _SEGAVIDEO_STATE_H
