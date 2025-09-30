// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Sample project and official ROM for the custom cartridge.  Streams video
// over WiFi.

#include <genesis.h>

#include "segavideo_menu.h"
#include "segavideo_player.h"
#include "segavideo_state.h"

#include "kinetoscope_logo.h"
#include "kinetoscope_startup_sound.h"

static void onJoystickEvent(u16 joystick, u16 changed, u16 state) {
  if (segavideo_getState() == Error) {
    // Error: press start|A to continue.
    if (state & (BUTTON_START | BUTTON_A)) {
      segavideo_menu_clearError();
    }
  } else if (segavideo_getState() == Player) {
    // Playing: press start to pause, C to stop.
    if (state & BUTTON_START) {
      segavideo_togglePause();
    }
    if (state & BUTTON_C) {
      segavideo_stop();
    }
  } else if (segavideo_getState() == Menu) {
    // Menu: press start|A to choose, up/down to navigate.
    if (state & (BUTTON_START | BUTTON_A)) {
      segavideo_menu_select(/* loop= */ false);
    }
    if (state & BUTTON_UP) {
      segavideo_menu_previousItem();
    }
    if (state & BUTTON_DOWN) {
      segavideo_menu_nextItem();
    }
  }
}

static void handleError() {
  segavideo_menu_showError();

  // Continue to show the error until the user presses something to clear it.
  while (segavideo_getState() == Error) {
    SYS_doVBlankProcess();
  }
}

static void startup_sequence() {
  // Set PAL0 to black.
  PAL_setPalette(PAL0, palette_black, CPU);

  // Load the image into VRAM.
  VDP_drawImageEx(
      /* plane= */ BG_B,
      &kinetoscope_logo,
      /* tile= */ TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, TILE_USER_INDEX),
      /* x= */ 2,
      /* y= */ 10,
      /* load palette= */ FALSE,
      /* use DMA= */ FALSE);

  // Fade in the image over 1.5 seconds (90 frames) asynchronously
  PAL_fadePalette(
      /* palette= */ PAL0,
      /* first colors= */ palette_black,
      /* final colors= */ kinetoscope_logo.palette->data,
      /* num frames= */ 90,
      /* async= */ TRUE);

  // Play the WAV file (2s) asynchronously, then pause for 1 more second
  SND_PCM_startPlay(
      /* sound= */ kinetoscope_startup_sound,
      /* length= */ sizeof(kinetoscope_startup_sound),
      /* rate= */ SOUND_PCM_RATE_22050,
      /* pan= */ SOUND_PAN_CENTER,
      /* loop= */ FALSE);
  waitMs(3000);
}

int main(bool hardReset) {
  JOY_setEventHandler(onJoystickEvent);
  segavideo_init();

  startup_sequence();

  segavideo_menu_init();

  // Stop immediately if we don't have the right hardware.
  if (!segavideo_menu_checkHardware()) {
    return 0;
  }

  while (true) {
    // Check for errors.  At this stage, most likely connection errors.
    if (segavideo_menu_hasError()) {
      handleError();
      continue;
    }

    // Start in the menu.
    if (segavideo_menu_load()) {
      segavideo_menu_draw();
    }

    // Check for errors.  May be download errors for the catalog.
    if (segavideo_menu_hasError()) {
      handleError();
      continue;
    }

    // Redraw the menu while it is visible.
    while (segavideo_getState() == Menu) {
      segavideo_menu_draw();
      SYS_doVBlankProcess();
    }

    // Check for errors.  May be download errors for a video.
    if (segavideo_menu_hasError()) {
      handleError();
      continue;
    }

    // While playing, process video frames.
    while (segavideo_isPlaying()) {
      segavideo_processFrames();
      SYS_doVBlankProcess();

      // Check for errors.  At this stage, most likely a buffer underflow.
      if (segavideo_menu_hasError()) {
        segavideo_stop();
        handleError();
        break;
      }
    }

    // Loop back to the menu.
  }

  return 0;
}
