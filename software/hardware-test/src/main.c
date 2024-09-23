// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Hardware test for the custom cartridge.

// TODO: Deduplicate constants and helpers.

#include <genesis.h>

#include "menu_font.h"
#include "segavideo_menu.h"
#include "segavideo_player.h"

// Ports to communicate with our special hardware.
#define KINETOSCOPE_PORT_COMMAND ((volatile uint16_t*)0xA13010)  // low 8 bits
#define KINETOSCOPE_PORT_ARG     ((volatile uint16_t*)0xA13012)  // low 8 bits
#define KINETOSCOPE_PORT_TOKEN   ((volatile uint16_t*)0xA13008)  // low 1 bit, set on write
#define KINETOSCOPE_PORT_ERROR   ((volatile uint16_t*)0xA1300A)  // low 1 bit, clear on write
#define KINETOSCOPE_DATA          ((volatile uint8_t*)0x200000)

// Commands for that hardware.
#define CMD_ECHO        0x00  // Writes arg to SRAM
#define CMD_GET_ERROR   0x05  // Load error information into SRAM
#define CMD_CONNECT_NET 0x06  // Connect to the network

// Palettes allocated for on-screen text.
#define PAL_WHITE  PAL2
#define PAL_YELLOW PAL3


static bool waitForToken(uint16_t timeout_seconds) {
  uint16_t counter = 0;
  uint16_t max_counter =
      IS_PAL_SYSTEM ? 50 * timeout_seconds : 60 * timeout_seconds;

  while (*KINETOSCOPE_PORT_TOKEN & 1) {
    if (++counter >= max_counter) {
      return false;
    }
    SYS_doVBlankProcess();
  }

  return true;
}


// Play from two SRAM regions:
int main(bool hardReset) {
  bool ok = false;

  segavideo_init();
  segavideo_menu_init();

  int line = 7;

  // 0. Print anything.  We should see this even if we hang on the next part.
  VDP_setTextPalette(PAL_WHITE);
  VDP_drawText("Beginning hardware test.", 0, line++);
  SYS_doVBlankProcess();

  // Wait for microcontroller initialization without any kind of active
  // handshake, since we are testing that here (among other things).  This is
  // much more than is needed, and allows time for the serial monitor to be
  // attached.
  waitMs(5 * 1000);


  // 1. Check initial state of error flag.
  if (*KINETOSCOPE_PORT_ERROR & 1) {
    VDP_setTextPalette(PAL_WHITE);
    VDP_drawText("Error flag set on boot.", 1, line++);
  } else {
    VDP_setTextPalette(PAL_WHITE);
    VDP_drawText("Error flag not set on boot.", 1, line++);
  }
  SYS_doVBlankProcess();


  // 2. Check our ability to clear the error flag.  (Weak test, may not have
  //    been set.)
  waitMs(1);
  *KINETOSCOPE_PORT_ERROR = 0;
  waitMs(1);

  if (*KINETOSCOPE_PORT_ERROR & 1) {
    VDP_setTextPalette(PAL_YELLOW);
    VDP_drawText("Unable to clear error flag.", 1, line++);
  } else {
    VDP_setTextPalette(PAL_WHITE);
    VDP_drawText("Error flag cleared.", 1, line++);
  }
  SYS_doVBlankProcess();


  // 3. Check initial state of command token.
  if (*KINETOSCOPE_PORT_TOKEN & 1) {
    VDP_setTextPalette(PAL_YELLOW);
    VDP_drawText("Command token set on boot.", 1, line++);
  } else {
    VDP_setTextPalette(PAL_WHITE);
    VDP_drawText("Command token not set on boot.", 1, line++);
  }
  SYS_doVBlankProcess();


  // 4. Try to send a command.
  waitMs(1);
  *KINETOSCOPE_PORT_COMMAND = CMD_ECHO;
  *KINETOSCOPE_PORT_ARG = 0;
  waitMs(1);
  *KINETOSCOPE_PORT_TOKEN = 1;
  SYS_doVBlankProcess();


  // 5. Wait for a reply.
  ok = waitForToken(/* timeout_seconds= */ 10);
  if (ok) {
    VDP_setTextPalette(PAL_WHITE);
    VDP_drawText("Echo command acknowleged.", 1, line++);
  } else {
    VDP_setTextPalette(PAL_YELLOW);
    VDP_drawText("Echo command timed out.", 1, line++);
  }
  SYS_doVBlankProcess();


  // 6. Send an invalid command.
  waitMs(1);
  *KINETOSCOPE_PORT_COMMAND = 0xFF;  // Invalid!
  *KINETOSCOPE_PORT_ARG = 0;
  waitMs(1);
  *KINETOSCOPE_PORT_TOKEN = 1;
  SYS_doVBlankProcess();


  // 7. Wait for a reply.
  ok = waitForToken(/* timeout_seconds= */ 10);
  if (ok) {
    VDP_setTextPalette(PAL_WHITE);
    VDP_drawText("Invalid command acknowleged.", 1, line++);
  } else {
    VDP_setTextPalette(PAL_YELLOW);
    VDP_drawText("Invalid command timed out.", 1, line++);
  }
  SYS_doVBlankProcess();


  // 8. Check state of error flag.
  if (*KINETOSCOPE_PORT_ERROR & 1) {
    VDP_setTextPalette(PAL_WHITE);
    VDP_drawText("Error flag set now.", 1, line++);
  } else {
    VDP_setTextPalette(PAL_YELLOW);
    VDP_drawText("Error flag not set.", 1, line++);
  }
  SYS_doVBlankProcess();


  // 9. Request error data.
  waitMs(1);
  *KINETOSCOPE_PORT_COMMAND = CMD_GET_ERROR;
  *KINETOSCOPE_PORT_ARG = 0;
  waitMs(1);
  *KINETOSCOPE_PORT_TOKEN = 1;
  SYS_doVBlankProcess();


  // 10. Wait for a reply.
  ok = waitForToken(/* timeout_seconds= */ 10);
  if (ok) {
    VDP_setTextPalette(PAL_WHITE);
    VDP_drawText("Get error command acknowleged.", 1, line++);
  } else {
    VDP_setTextPalette(PAL_YELLOW);
    VDP_drawText("Get error command timed out.", 1, line++);
  }
  SYS_doVBlankProcess();


  // 11. Check error data from SRAM.
  char error[256];
  for (int i = 0; i < 256; ++i) {
    uint8_t data = KINETOSCOPE_DATA[i];
    if (data >= 32 && data <= 127) {
      error[i] = (char)data;
    } else if (data == 0) {
      error[i] = '\0';
      break;
    } else {
      error[i] = '?';
    }
  }
  error[255] = '\0';

  VDP_setTextPalette(PAL_WHITE);
  VDP_drawText("Error data in SRAM:", 1, line++);
  VDP_setTextPalette(PAL_YELLOW);
  VDP_drawText(error, 3, line++);
  SYS_doVBlankProcess();


  // 12. Test echoing various values through SRAM.  FIXME: Only verifies the
  // first byte of the first bank of SRAM.
  uint8_t results[64];  // 4 bits each.
  char resultMessage[65];  // hex character.
  const char* hex_alphabet = "0123456789ABCDEF";
  for (int i = 0; i < 64; ++i) {
    results[i] = 0;
    resultMessage[i] = ' ';
  }
  resultMessage[64] = '\0';

  VDP_setTextPalette(PAL_WHITE);
  VDP_drawText("Echo test results:", 1, line++);
  line += 2;  // Consume two lines for results

  bool all_passed = true;
  for (int i = 0; i < 256; ++i) {
    waitMs(1);
    *KINETOSCOPE_PORT_COMMAND = CMD_ECHO;
    *KINETOSCOPE_PORT_ARG = i;
    waitMs(1);
    *KINETOSCOPE_PORT_TOKEN = 1;
    SYS_doVBlankProcess();

    if (!waitForToken(/* timeout_seconds= */ 10)) {
      VDP_setTextPalette(PAL_YELLOW);
      VDP_drawText("Echo command timed out.", 1, line++);
      break;
    }
    SYS_doVBlankProcess();

    uint8_t data = *KINETOSCOPE_DATA;
    if (data == i) {
      results[i / 4] |= 0x08 >> (i % 4);  // 0x08, 0x04, 0x02, 0x01
    } else {
      all_passed = false;
    }
    resultMessage[i / 4] = hex_alphabet[results[i / 4]];

    if (i % 4 == 3) {
      VDP_setTextPalette(PAL_YELLOW);
      VDP_drawText(resultMessage, 0, line - 2);
      VDP_drawText(resultMessage + 32, 0, line - 1);
    }
  }

  if (all_passed) {
    VDP_setTextPalette(PAL_WHITE);
    VDP_drawText("Echo test complete.", 1, line++);
  } else {
    VDP_setTextPalette(PAL_YELLOW);
    VDP_drawText("Echo test failed.", 1, line++);
  }


  while (true) {
    waitMs(1000);
  }

  return 0;
}
