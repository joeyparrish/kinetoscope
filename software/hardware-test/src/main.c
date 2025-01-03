// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Hardware test for the custom cartridge.

// TODO: Deduplicate constants and helpers.

#include <genesis.h>

#include "menu_font.h"
#include "segavideo_format.h"
#include "segavideo_menu.h"
#include "segavideo_player.h"

// Ports to communicate with our special hardware.
#define KINETOSCOPE_PORT_COMMAND ((volatile uint16_t*)0xA13010)  // low 8 bits
#define KINETOSCOPE_PORT_ARG     ((volatile uint16_t*)0xA13012)  // low 8 bits
#define KINETOSCOPE_PORT_TOKEN   ((volatile uint16_t*)0xA13008)  // low 1 bit, set on write
#define KINETOSCOPE_PORT_ERROR   ((volatile uint16_t*)0xA1300A)  // low 1 bit, clear on write
#define KINETOSCOPE_DATA          ((volatile const uint8_t*)0x200000)
#define KINETOSCOPE_SRAM_BANK_0   ((volatile const uint8_t*)0x200000)
#define KINETOSCOPE_SRAM_BANK_1   ((volatile const uint8_t*)0x300000)

// Commands for that hardware.
#define CMD_ECHO        0x00  // Writes arg to SRAM
#define CMD_LIST_VIDEOS 0x01  // Writes video list to SRAM
#define CMD_START_VIDEO 0x02  // Begins streaming to SRAM
#define CMD_GET_ERROR   0x05  // Load error information into SRAM
#define CMD_CONNECT_NET 0x06  // Connect to the network
#define CMD_MARCH_TEST  0x07  // Test SRAM

// Palettes allocated for on-screen text.
#define PAL_WHITE  PAL2
#define PAL_YELLOW PAL3

// Maximum number of entries in a video catalog file.
#define MAX_CATALOG_SIZE 127

// Macros to complete sram_march_test in sram-common.h
char march_error_1[256];
char march_error_2[256];
#define SRAM_MARCH_TEST_START(bank) \
  volatile const uint8_t* sram = bank ? KINETOSCOPE_SRAM_BANK_1 : KINETOSCOPE_SRAM_BANK_0
#define SRAM_MARCH_TEST_DATA(offset, data) { \
  uint8_t real = sram[offset]; \
  if (real != data) { \
    sprintf(march_error_1, "Fail at offset %ld      ", offset); \
    sprintf(march_error_2, "Expected 0x%02x, got 0x%02x", \
            (int)data, (int)real); \
    return false; \
  } \
}
#define SRAM_MARCH_TEST_END() {}

// Defines sram_march_test()
#include "sram-common.h"


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
  VDP_drawText("Beginning hardware test...", 0, line++);
  line++;  // blank line

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


  // 3. Check initial state of command token.
  if (*KINETOSCOPE_PORT_TOKEN & 1) {
    VDP_setTextPalette(PAL_YELLOW);
    VDP_drawText("Command token set on boot.", 1, line++);
  } else {
    VDP_setTextPalette(PAL_WHITE);
    VDP_drawText("Command token not set on boot.", 1, line++);
  }


  // 4. Try to send a command.
  waitMs(1);
  *KINETOSCOPE_PORT_COMMAND = CMD_ECHO;
  *KINETOSCOPE_PORT_ARG = 0;
  waitMs(1);
  *KINETOSCOPE_PORT_TOKEN = 1;


  // 5. Wait for a reply.
  ok = waitForToken(/* timeout_seconds= */ 10);
  if (ok) {
    VDP_setTextPalette(PAL_WHITE);
    VDP_drawText("Echo command acknowleged.", 1, line++);
  } else {
    VDP_setTextPalette(PAL_YELLOW);
    VDP_drawText("Echo command timed out.", 1, line++);
  }


  // 6. Send an invalid command.
  waitMs(1);
  *KINETOSCOPE_PORT_COMMAND = 0xFF;  // Invalid!
  *KINETOSCOPE_PORT_ARG = 0;
  waitMs(1);
  *KINETOSCOPE_PORT_TOKEN = 1;


  // 7. Wait for a reply.
  ok = waitForToken(/* timeout_seconds= */ 10);
  if (ok) {
    VDP_setTextPalette(PAL_WHITE);
    VDP_drawText("Invalid command acknowleged.", 1, line++);
  } else {
    VDP_setTextPalette(PAL_YELLOW);
    VDP_drawText("Invalid command timed out.", 1, line++);
  }


  // 8. Check state of error flag.
  if (*KINETOSCOPE_PORT_ERROR & 1) {
    VDP_setTextPalette(PAL_WHITE);
    VDP_drawText("Error flag set now.", 1, line++);
  } else {
    VDP_setTextPalette(PAL_YELLOW);
    VDP_drawText("Error flag not set.", 1, line++);
  }


  // 9. Request error data.
  waitMs(1);
  *KINETOSCOPE_PORT_COMMAND = CMD_GET_ERROR;
  *KINETOSCOPE_PORT_ARG = 0;
  waitMs(1);
  *KINETOSCOPE_PORT_TOKEN = 1;


  // 10. Wait for a reply.
  ok = waitForToken(/* timeout_seconds= */ 10);
  if (ok) {
    VDP_setTextPalette(PAL_WHITE);
    VDP_drawText("Get error command acknowleged.", 1, line++);
  } else {
    VDP_setTextPalette(PAL_YELLOW);
    VDP_drawText("Get error command timed out.", 1, line++);
  }


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

  const char* expected_error = "Unrecognized command 0xFF!";
  // NOTE: There is no memcmp in SGDK.
  bool error_matches = true;
  for (int i = 0; i < strlen(expected_error) + 1; ++i) {
    if (error[i] != expected_error[i]) {
      error_matches = false;
      break;
    }
  }
  if (error_matches) {
    VDP_setTextPalette(PAL_WHITE);
    VDP_drawText("Found expected message in SRAM.", 1, line++);
  } else {
    VDP_setTextPalette(PAL_WHITE);
    VDP_drawText("Unexpected message in SRAM:", 1, line++);
    VDP_setTextPalette(PAL_YELLOW);
    VDP_drawText(error, 3, line++);
  }


  // 12. Basic echo handshake that the streamer ROM will do.
  int echo_test_line = line++;
  VDP_setTextPalette(PAL_WHITE);
  //           0         1
  //           012345678901234
  VDP_drawText("Echo test ...", 1, echo_test_line);

  uint8_t echo_data[2] = {0x55, 0xAA};
  int status_x[2] = {11, 12};

  for (int i = 0; i < 2; ++i) {
    waitMs(1);
    *KINETOSCOPE_PORT_COMMAND = CMD_ECHO;
    *KINETOSCOPE_PORT_ARG = echo_data[i];
    waitMs(1);
    *KINETOSCOPE_PORT_TOKEN = 1;

    if (!waitForToken(/* timeout_seconds= */ 2)) {
      VDP_setTextPalette(PAL_YELLOW);
      VDP_drawText("Echo command timed out.", 1, line++);
      break;
    }

    uint8_t data = *KINETOSCOPE_DATA;
    bool pass = data == echo_data[i];
    VDP_setTextPalette(pass ? PAL_WHITE : PAL_YELLOW);
    VDP_drawText(pass ? "P" : "F", status_x[i], echo_test_line);
  }


  // 13. Test network connectivity and writing data to SRAM.
  do {
    *KINETOSCOPE_PORT_ERROR = 0;
    *KINETOSCOPE_PORT_COMMAND = CMD_CONNECT_NET;
    *KINETOSCOPE_PORT_TOKEN = 1;
    if (!waitForToken(/* timeout_seconds= */ 30)) {
      VDP_setTextPalette(PAL_YELLOW);
      VDP_drawText("Network command timed out!", 1, line++);
      break;
    } else if (*KINETOSCOPE_PORT_ERROR & 1) {
      VDP_setTextPalette(PAL_YELLOW);
      VDP_drawText("Unable to connect to network!", 1, line++);
      break;
    } else {
      VDP_setTextPalette(PAL_WHITE);
      VDP_drawText("Network connected.", 1, line++);
    }

    *KINETOSCOPE_PORT_COMMAND = CMD_LIST_VIDEOS;
    *KINETOSCOPE_PORT_TOKEN = 1;
    if (!waitForToken(/* timeout_seconds= */ 30)) {
      VDP_setTextPalette(PAL_YELLOW);
      VDP_drawText("List videos command timed out!", 1, line++);
      break;
    } else if (*KINETOSCOPE_PORT_ERROR & 1) {
      VDP_setTextPalette(PAL_YELLOW);
      VDP_drawText("List videos command failed!", 1, line++);
      break;
    }

    if (!segavideo_validateHeader((const uint8_t*)KINETOSCOPE_DATA)) {
      VDP_setTextPalette(PAL_YELLOW);
      VDP_drawText("Catalog header invalid!", 1, line++);
      break;
    } else {
      VDP_setTextPalette(PAL_WHITE);
      VDP_drawText("Catalog header validated.", 1, line++);
    }
  } while (false);


  // 14. Count the size of the catalog.
  const SegaVideoHeader* header = (const SegaVideoHeader*)KINETOSCOPE_DATA;
  int num_videos = 0;
  while (header->magic[0]) {
    num_videos++;
    header++;

    if (num_videos > MAX_CATALOG_SIZE) {
      VDP_clearTextArea(0, line, 32, 1);
      VDP_setTextPalette(PAL_YELLOW);
      VDP_drawText("Video catalog overflow!", 1, line++);
      break;
    }

    VDP_clearTextArea(0, line, 32, 1);
    VDP_setTextPalette(PAL_WHITE);
    VDP_drawText(header->title, 0, line);
  }
  char catalog_count_buf[32];
  //                          0         1         2         3
  //                          0123456789012345678901234567890
  //                                        12345678901
  sprintf(catalog_count_buf, "Catalog size: %d", num_videos);
  VDP_setTextPalette(PAL_WHITE);
  VDP_drawText(catalog_count_buf, 0, line++);


  // 15. Attempt to start streaming a video.
  do {
    *KINETOSCOPE_PORT_COMMAND = CMD_START_VIDEO;
    *KINETOSCOPE_PORT_ARG = 0;
    *KINETOSCOPE_PORT_TOKEN = 1;
    if (!waitForToken(/* timeout_seconds= */ 30)) {
      VDP_setTextPalette(PAL_YELLOW);
      VDP_drawText("Start video command timed out!", 1, line++);
      break;
    } else if (*KINETOSCOPE_PORT_ERROR & 1) {
      VDP_setTextPalette(PAL_YELLOW);
      VDP_drawText("Start video command failed!", 1, line++);
      break;
    }

    if (!segavideo_validateHeader((const uint8_t*)KINETOSCOPE_DATA)) {
      VDP_setTextPalette(PAL_YELLOW);
      VDP_drawText("Video header invalid!", 1, line++);
      break;
    } else {
      VDP_setTextPalette(PAL_WHITE);
      VDP_drawText("Video header validated.", 1, line++);
    }
  } while (false);


  // 16. Perform various intensive memory tests through the firmware.
  // There are many different passes of this, with different patterns to verify.
  line++;  // blank line
  int memory_test_pass_line = line;
  line += 2;
  int memory_test_error_line = line;
  line += 2;

  VDP_setTextPalette(PAL_WHITE);
  //            0         1
  //            012345678901234567
  VDP_drawText("SRAM test pass 00", 0, memory_test_pass_line);
  VDP_drawText("......................", 1, memory_test_pass_line + 1);

  for (int pass = 0; pass < SRAM_MARCH_TEST_NUM_PASSES; ++pass) {
    waitMs(1);
    *KINETOSCOPE_PORT_COMMAND = CMD_MARCH_TEST;
    *KINETOSCOPE_PORT_ARG = pass;
    waitMs(1);
    *KINETOSCOPE_PORT_TOKEN = 1;

    VDP_setTextPalette(PAL_WHITE);
    char counter[3] = {
      '0' + ((pass / 10) % 10),
      '0' + (pass % 10),
      '\0',
    };
    VDP_drawText(counter, 15, memory_test_pass_line);
    // "!": indicates that the uC is filling the SRAM with data.
    VDP_drawText("!", pass + 1, memory_test_pass_line + 1);

    if (!waitForToken(/* timeout_seconds= */ 30)) {
      VDP_setTextPalette(PAL_YELLOW);
      VDP_drawText("SRAM test command timed out.", 1, line++);
      break;
    }
    // "?": indicates that the Sega is reading the SRAM.
    VDP_drawText("?", pass + 1, memory_test_pass_line + 1);

    bool ok = sram_march_test(pass);
    VDP_setTextPalette(ok ? PAL_WHITE : PAL_YELLOW);
    VDP_drawText(ok ? "P" : "F", pass + 1, memory_test_pass_line + 1);
    if (!ok) {
      VDP_drawText(march_error_1, 0, memory_test_error_line);
      VDP_drawText(march_error_2, 0, memory_test_error_line + 1);
      waitMs(10 * 1000);
    }
  }


  // 16. Done!
  line++;  // blank line
  VDP_drawText("Testing complete!", 0, line++);
  while (true) {
    waitMs(1000);
  }

  return 0;
}
