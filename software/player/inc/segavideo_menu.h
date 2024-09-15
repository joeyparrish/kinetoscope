// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Sega video player menu interface.

#ifndef _SEGAVIDEO_MENU_H
#define _SEGAVIDEO_MENU_H

#include <genesis.h>

#include "segavideo_state.h"

// Each of these functions requires special hardware or emulation.

// Initialize everything needed to run the menu.  Must be called before any of
// these other methods.
void segavideo_menu_init();

// Returns true if the streaming hardware is available.
// Shows status and error messages on screen during this process.
bool segavideo_menu_checkHardware();

// Fetch a list of available videos and write it to memory.
// Shows an error message on-screen and returns false on failure.
bool segavideo_menu_load();

// Draw the on-screen menu of videos.
// Only valid after calling segavideo_getMenu() returns true.
void segavideo_menu_draw();

// Move to the previous menu item.
void segavideo_menu_previousItem();

// Move to the next menu item.
void segavideo_menu_nextItem();

// Start streaming the currently-selected menu item.
// Shows an error message on-screen and returns false on failure.
bool segavideo_menu_select(bool loop);

// Is there a pending error or an error already on screen?
// This is not valid until after segavideo_menu_checkHardware() has succeeded.
// Before that time, the state of the error flag is undefined.
bool segavideo_menu_hasError();

// Show the error.
// This is not valid until after segavideo_menu_checkHardware() has succeeded.
// Before that time, the state of the error flag is undefined.
void segavideo_menu_showError();

// Clear the error state and screen.
// The error is automatically cleared during a successful call to
// segavideo_menu_checkHardware().
void segavideo_menu_clearError();

#endif // _SEGAVIDEO_MENU_H
