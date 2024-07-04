// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Sega video state.  Internal methods that shouldn't be called by your app.
//
// This can run on the Sega, inside an emulator, or in the firmware of the
// streaming hardware.

#ifndef _SEGAVIDEO_STATE_INTERNAL_H
#define _SEGAVIDEO_STATE_INTERNAL_H

#include "segavideo_state.h"

// Sets the state.
void segavideo_setState(SegaVideoState state);

#endif // _SEGAVIDEO_STATE_INTERNAL_H
