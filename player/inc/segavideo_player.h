// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Sega video player and streaming hardware interface.

#ifndef _SEGAVIDEO_PLAYER_H
#define _SEGAVIDEO_PLAYER_H

#include <genesis.h>

// Initialize everything needed to play video.  Must be called before any of
// these other methods.
void segavideo_init();

// Start a specific video by its memory address. Use this for videos embedded
// in the ROM.
void segavideo_play(const uint8_t* videoData, bool loop);

// Call this from the main loop before SYS_doVBlankProcess().
void segavideo_processFrames();

// Pauses the video.
void segavideo_pause();

// Resumes the video.
void segavideo_resume();

// Toggle the paused state.
void segavideo_togglePause();

// Stops the video. Can be started again with segavideo_start() or
// segavideo_stream(). Does not require another call to segavideo_init().
void segavideo_stop();

// True if we are playing something.
bool segavideo_isPlaying();

// -- --- ----- Streaming APIs ----- --- -- //
// Each of these functions requires special hardware or emulation.

// Returns true if the streaming hardware is available.
// Shows status and error messages on screen during this process.
bool segavideo_checkHardware();

// Fetch a list of available videos and write it to memory.
// Shows an error message on-screen and returns false on failure.
bool segavideo_getMenu();

// Draw the on-screen menu of videos.
// Only valid after calling segavideo_getMenu() returns true.
void segavideo_drawMenu();

// True if the menu is showing.
bool segavideo_isMenuShowing();

// Move to the previous menu item.
void segavideo_menuPreviousItem();

// Move to the next menu item.
void segavideo_menuNextItem();

// Start streaming the currently-selected menu item.
// Shows an error message on-screen and returns false on failure.
bool segavideo_stream(bool loop);

#endif // _SEGAVIDEO_PLAYER_H
