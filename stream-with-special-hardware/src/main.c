// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Sample project and official ROM for the custom cartridge.  Streams video
// over WiFi.

#include <genesis.h>

#include "segavideo_player.h"

static void onJoystickEvent(u16 joystick, u16 changed, u16 state) {
  if (segavideo_isPlaying()) {
    if (state & BUTTON_START) {
      segavideo_togglePause();
    }
  } else if (segavideo_isMenuShowing()) {
    if (state & BUTTON_START) {
      segavideo_stream(/* loop= */ false);
    }
    if (state & BUTTON_UP) {
      segavideo_menuPreviousItem();
    }
    if (state & BUTTON_DOWN) {
      segavideo_menuNextItem();
    }
  }
}

int main(bool hardReset) {
  JOY_setEventHandler(onJoystickEvent);

  segavideo_init();

  if (!segavideo_checkHardware() || !segavideo_getMenu()) {
    return 0;
  }

  while (true) {
    // Start in the menu.
    segavideo_drawMenu();

    // Redraw the menu while it is visible.
    while (segavideo_isMenuShowing()) {
      segavideo_drawMenu();
      SYS_doVBlankProcess();
    }

    // While playing, process video frames.
    while (segavideo_isPlaying()) {
      segavideo_processFrames();
      SYS_doVBlankProcess();
    }

    // Loop back to the menu.
  }

  return 0;
}
