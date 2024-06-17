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
  } else if (segavideo_isErrorShowing()) {
    if (state & BUTTON_START) {
      segavideo_clearError();
    }
  }
}

static void handleError() {
  while (segavideo_hasError()) {
    segavideo_showError();
    SYS_doVBlankProcess();
  }
}

int main(bool hardReset) {
  JOY_setEventHandler(onJoystickEvent);

  segavideo_init();

  if (!segavideo_checkHardware()) {
    return 0;
  }

  while (true) {
    if (segavideo_hasError()) {
      handleError();
      continue;
    }

    // Start in the menu.
    if (segavideo_getMenu()) {
      segavideo_drawMenu();
    }

    // Redraw the menu while it is visible.
    while (segavideo_isMenuShowing()) {
      segavideo_drawMenu();
      SYS_doVBlankProcess();
    }

    if (segavideo_hasError()) {
      handleError();
      continue;
    }

    // While playing, process video frames.
    while (segavideo_isPlaying()) {
      segavideo_processFrames();
      SYS_doVBlankProcess();
    }

    if (segavideo_hasError()) {
      handleError();
      continue;
    }

    // Loop back to the menu.
  }

  return 0;
}
