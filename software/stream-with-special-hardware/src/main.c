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
    // Playing: press start to pause, C to stop.
    if (state & BUTTON_START) {
      segavideo_togglePause();
    }
    if (state & BUTTON_C) {
      segavideo_stop();
    }
  } else if (segavideo_isMenuShowing()) {
    // Menu: press start to choose, up/down to navigate.
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
    // Error: press start to continue.
    if (state & BUTTON_START) {
      segavideo_clearError();
    }
  }
}

static void handleError() {
  // Continue to show the error until the user presses start to clear it.
  while (segavideo_hasError()) {
    segavideo_showError();
    SYS_doVBlankProcess();
  }
}

int main(bool hardReset) {
  JOY_setEventHandler(onJoystickEvent);

  segavideo_init();

  // Stop immediately if we don't have the right hardware.
  if (!segavideo_checkHardware()) {
    return 0;
  }

  while (true) {
    // Check for errors.  At this stage, most likely connection errors.
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

    // Check for errors.  May be download errors for the menu or a video.
    if (segavideo_hasError()) {
      handleError();
      continue;
    }

    // While playing, process video frames.
    while (segavideo_isPlaying()) {
      segavideo_processFrames();
      SYS_doVBlankProcess();

      // Check for errors.  At this stage, most likely a buffer underflow.
      if (segavideo_hasError()) {
        handleError();
        break;
      }
    }

    // Loop back to the menu.
  }

  return 0;
}
