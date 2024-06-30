// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Sample project that plays embedded video from the ROM.

#include <genesis.h>

#include "segavideo_player.h"
#include "video_data.h"

static void onJoystickEvent(u16 joystick, u16 changed, u16 state) {
  if (segavideo_isPlaying()) {
    if (state & BUTTON_START) {
      segavideo_togglePause();
    }
  }
}

int main(bool hardReset) {
  JOY_setEventHandler(onJoystickEvent);

  segavideo_init();

  segavideo_play(video_data, /* loop= */ true);

  while (true) {
    // This order reduces screen tearing:
    segavideo_processFrames();
    SYS_doVBlankProcess();
  }

  return 0;
}
