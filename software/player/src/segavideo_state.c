// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Sega video state tracking, shared between modules.

#include "segavideo_state_internal.h"

static SegaVideoState state = Idle;

void segavideo_setState(SegaVideoState newState) {
  state = newState;
}

SegaVideoState segavideo_getState() {
  return state;
}
