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

// For internal use in the streamer ROM.  Don't use this.
bool segavideo_validateHeader(const uint8_t* videoData);

// For internal use in the streamer ROM.  Don't use this.
bool segavideo_playInternal(const uint8_t* videoData, bool pleaseLoop,
                            uint32_t pleaseRegionSize,
                            uint32_t pleaseRegionMask,
                            VoidCallback* pleaseLoopCallback,
                            VoidCallback* pleaseStopCallback,
                            VoidCallback* pleaseFlipCallback,
                            VoidCallback* pleaseEmuHackCallback);

#endif // _SEGAVIDEO_PLAYER_H
