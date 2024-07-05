// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Sega menu interface.

#include <genesis.h>

#include "segavideo_menu.h"
#include "segavideo_format.h"
#include "segavideo_parser.h"
#include "segavideo_player.h"
#include "segavideo_state_internal.h"

#include "kinetoscope_logo.h"
#include "trivial_tilemap.h"

static bool menuChanged;
static char **menuLines;
static int numVideos;
static int selectedIndex;

#define MENU_ITEM_X 2   // tiles
#define MENU_ITEM_Y_MULTIPLIER 2
#define MENU_Y_OFFSET 9 // tiles
#define MENU_SELECTOR_X_OFFSET -2 // tiles

#define STATUS_MESSAGE_X 1
#define STATUS_MESSAGE_Y 7

#define LOGO_X 2 // tiles
#define LOGO_Y 1 // tiles

// Hard-coded for now.  Fullscreen video only.
#define MAP_W 32
#define MAP_H 28
#define NUM_TILES (32 * 28)  // 896

#define THUMB_X 15
#define THUMB_Y 13
#define THUMB_MAP_W 16
#define THUMB_MAP_H 14
#define NUM_THUMB_TILES (16 * 14)  // 224

// NOTE: The thumbnail occupies 224 tiles, 1 through 224
// NOTE: The logo occupies 168 tiles, right after the thumbnail
// NOTE: The font occupies 96 tiles, 1696 through 1791

#define TILE_SIZE 32
#define MAX_CATALOG_SIZE 127

#if defined(SIMULATE_HARDWARE)
# include "embedded_catalog.h"
# include "embedded_video.h"

# define KINETOSCOPE_MENU_DATA embedded_catalog
# define KINETOSCOPE_ERROR_DATA "Error: something went wrong!"
# define KINETOSCOPE_VIDEO_DATA embedded_video
# define KINETOSCOPE_VIDEO_REGION_SIZE 0
# define KINETOSCOPE_VIDEO_REGION_MASK 0
#else
// Ports to communicate with our special hardware.
# define KINETOSCOPE_PORT_COMMAND (volatile uint16_t*)0xA13000  // low 8 bits
# define KINETOSCOPE_PORT_ARG     (volatile uint16_t*)0xA13002  // low 8 bits
# define KINETOSCOPE_PORT_TOKEN   (volatile uint16_t*)0xA13008  // low 1 bit, set on write
# define KINETOSCOPE_PORT_ERROR   (volatile uint16_t*)0xA1300A  // low 1 bit, clear on write
# define KINETOSCOPE_DATA          (volatile uint8_t*)0x200000
# define KINETOSCOPE_MENU_DATA        (const uint8_t*)(KINETOSCOPE_DATA)
# define KINETOSCOPE_ERROR_DATA          (const char*)(KINETOSCOPE_DATA)

// Play from two SRAM regions:
//  - starting at 0x200000 and ending at 0x300000
//  - starting at 0x300000 and ending at 0x400000
// The streamer hardware will fill in whole chunks only into these regions,
// flipping back and forth between them.
# define KINETOSCOPE_VIDEO_DATA       (const uint8_t*)(KINETOSCOPE_DATA)
# define KINETOSCOPE_VIDEO_REGION_SIZE 0x100000  // 1MB
# define KINETOSCOPE_VIDEO_REGION_MASK 0x300000
#endif

// Commands for that hardware.
#define CMD_ECHO        0x00  // Writes arg to SRAM
#define CMD_LIST_VIDEOS 0x01  // Writes video list to SRAM
#define CMD_START_VIDEO 0x02  // Begins streaming to SRAM
#define CMD_STOP_VIDEO  0x03  // Stops streaming
#define CMD_FLIP_REGION 0x04  // Switch SRAM banks for streaming
#define CMD_GET_ERROR   0x05  // Load error information into SRAM

// Token values for async communication.
#define TOKEN_CONTROL_TO_SEGA     0
#define TOKEN_CONTROL_TO_STREAMER 1

// Palettes allocated for logo and thumbnail.
#define PAL_THUMB  PAL0
#define PAL_LOGO   PAL1
// Palettes allocated for on-screen text.
#define PAL_WHITE  PAL2
#define PAL_YELLOW PAL3

#if (DEBUG == 0)
# define kprintf(...)
#endif

static bool sendCommand(uint16_t command, uint16_t arg0) {
#if !defined(SIMULATE_HARDWARE)
  volatile uint16_t* command_port = KINETOSCOPE_PORT_COMMAND;
  volatile uint16_t* token_port = KINETOSCOPE_PORT_TOKEN;
  volatile uint16_t* arg_port = KINETOSCOPE_PORT_ARG;

  if (*token_port != TOKEN_CONTROL_TO_SEGA) {
    return false;
  }

  *command_port = command;
  *arg_port = arg0;
  *token_port = TOKEN_CONTROL_TO_STREAMER;
#endif
  return true;
}

static bool waitForReply(uint16_t timeout_seconds) {
#if !defined(SIMULATE_HARDWARE)
  kprintf("Waiting for streamer response.\n");

  volatile uint16_t* token_port = KINETOSCOPE_PORT_TOKEN;
  uint16_t counter = 0;
  uint16_t max_counter =
      IS_PAL_SYSTEM ? 50 * timeout_seconds : 60 * timeout_seconds;
  while (*token_port == TOKEN_CONTROL_TO_STREAMER && ++counter < max_counter) {
    SYS_doVBlankProcess();
  }

  if (*token_port == TOKEN_CONTROL_TO_STREAMER) {
    return false;
  }

#endif
  return true;
}

static bool sendCommandAndWait(
    uint16_t command, uint16_t arg0, uint16_t timeout_seconds) {
  return sendCommand(command, arg0) && waitForReply(timeout_seconds);
}

static bool pendingError() {
#if defined(SIMULATE_HARDWARE)
  return false;
#else
  volatile uint16_t* error_port = KINETOSCOPE_PORT_ERROR;
  return *error_port != 0;
#endif
}

static void clearPendingError() {
#if !defined(SIMULATE_HARDWARE)
  volatile uint16_t* error_port = KINETOSCOPE_PORT_ERROR;
  *error_port = 0;
#endif
}

static void clearScreen() {
  VDP_clearPlane(BG_B, /* wait= */ true);
}

static void loadMenuColors() {
  // Load menu colors.
  uint16_t white  = 0x0FFF;  // ABGR
  uint16_t yellow = 0x00FF;  // ABGR
  // Text uses the last entry of each palette.
  PAL_setColors(PAL_WHITE  * 16 + 15, &white,  /* count= */ 1, CPU);
  PAL_setColors(PAL_YELLOW * 16 + 15, &yellow, /* count= */ 1, CPU);
}

static void drawMultilineText(const char* text) {
  const int max_line_len = 30;
  char line[31];  // max_line_len + nul terminator

  int len = strlen(text);
  int y = STATUS_MESSAGE_Y;
  kprintf("drawMultilineText: %s\n", text);
  kprintf("drawMultilineText: len=%d\n", (int)len);

  // Could go negative after ending on the nul, hence > 0 and not != 0
  while (len > 0) {
    // Maximum length available for this line.
    int max_len = len > max_line_len ? max_line_len : len;

    // Find the last space where we could break the line without overflow.
    int space_offset = max_len;
    while (space_offset >= 0 &&
           text[space_offset] != ' ' &&
           text[space_offset] != '\0') {
      space_offset--;
    }

    if (space_offset < 0) {
      // The first word is too long.  Truncate it, then skip the rest.
      memcpy(line, text, max_len);
      line[max_len] = '\0';

      while (*text != ' ' && *text != '\0') {
        len--;
        text++;
      }
    } else {
      // |space_offset| is the space on which we must break the line.
      memcpy(line, text, space_offset);
      line[space_offset] = '\0';

      len -= space_offset + 1;
      text += space_offset + 1;
    }

    kprintf("drawMultilineText: y=%d, len=%d, line=%s\n", y, len, line);
    VDP_clearTextArea(STATUS_MESSAGE_X, y, max_line_len, 1);
    VDP_drawText(line, STATUS_MESSAGE_X, y);
    y++;
  }
}

static void genericMessage(uint16_t pal, const char* message) {
  // Load the default font (which may have been overwritten by video playback).
  VDP_loadFont(&font_default, CPU);

  // Set the palette.
  VDP_setTextPalette(pal);

  // Put the message on the screen.
  drawMultilineText(message);

  // Send the message to the emulator's debug interface (if available).
  kprintf("%s\n", message);
}

static void statusMessage(const char* message) {
  genericMessage(PAL_WHITE, message);
}

static void errorMessage(const char* message) {
  genericMessage(PAL_YELLOW, message);
  segavideo_setState(Error);
}

void segavideo_menu_init() {
  kprintf("segavideo_menu_init\n");

  // Load menu palettes.
  loadMenuColors();

  // Allocate menu line pointers.
  menuLines = (char**)MEM_alloc(sizeof(char*) * MAX_CATALOG_SIZE);
  // Allocate a full row of text for each line.
  for (uint16_t i = 0; i < MAX_CATALOG_SIZE; ++i) {
    // One extra for nul terminator:
    menuLines[i] = (char*)MEM_alloc(MAP_W + 1);
    memset(menuLines[i], 0, MAP_W + 1);
  }

  selectedIndex = 0;
  numVideos = 0;
  menuChanged = false;
}

static void drawLogo() {
  VDP_drawImageEx(
      BG_B, &kinetoscope_logo,
      TILE_ATTR_FULL(PAL_LOGO, FALSE, FALSE, FALSE, NUM_THUMB_TILES + 1),
      LOGO_X, LOGO_Y,
      /* load palette */ TRUE,
      CPU);

  // FIXME: What is overwriting these?
  loadMenuColors();
}

bool segavideo_menu_checkHardware() {
  clearScreen();
  drawLogo();

  statusMessage("Checking for Kinetoscope cartridge...");

#if defined(SIMULATE_HARDWARE)
  waitMs(1000);
#else
  uint16_t command_timeout = 5; // seconds
  volatile uint8_t* data = KINETOSCOPE_DATA;

  if (!sendCommand(CMD_ECHO, 0x55)) {
    errorMessage("Kinetoscope cartridge not found! (code 1)");
    kprintf("The token was in an invalid state. Streamer hardware unlikely.\n");
    return false;
  }

  if (!waitForReply(command_timeout)) {
    errorMessage("Kinetoscope cartridge not found! (code 2)");
    kprintf("No reply from streamer hardware before timeout.\n");
    return false;
  }

  if (*data != 0x55) {
    errorMessage("Kinetoscope cartridge not found! (code 3)");
    kprintf("Unable to find 0x55 echoed back: %d\n", *data);
    return false;
  }

  if (!sendCommandAndWait(CMD_ECHO, 0xAA, command_timeout)) {
    errorMessage("Kinetoscope cartridge not found! (code 4)");
    return false;
  }

  if (*data != 0xAA) {
    errorMessage("Kinetoscope cartridge not found! (code 5)");
    kprintf("Unable to find 0xAA echoed back: %d\n", *data);
    return false;
  }
#endif

  statusMessage("Kinetoscope cartridge detected!");
  waitMs(1000);
  return true;
}

bool segavideo_menu_load() {
  statusMessage("Fetching video list...");

  uint16_t command_timeout = 30; // seconds
  if (!sendCommandAndWait(CMD_LIST_VIDEOS, 0, command_timeout)) {
    errorMessage("Failed to fetch video list!");
    return false;
  }

  const uint8_t* data = KINETOSCOPE_MENU_DATA;

  // Validate the catalog header.
  if (!segavideo_validateHeader(data)) {
    errorMessage("Video catalog is invalid!");
    return false;
  }

  // Count the number of entries in the catalog and copy their titles.
  const SegaVideoHeader* header = (const SegaVideoHeader*)data;
  numVideos = 0;
  while (header->magic[0]) {
    // This relies on header->title (128 bytes) being larger than menuLines[x]
    // (32 bytes) and zero-padded.
    memcpy(menuLines[numVideos], header->title, MAP_W);
    menuLines[numVideos][MAP_W] = '\0';  // Allocated MAP_W+1 bytes

    numVideos++;
    header++;

    if (numVideos > MAX_CATALOG_SIZE) {
      errorMessage("Video catalog overflow!");
      return false;
    }
  }

  selectedIndex = 0;
  return true;
}

static void drawMenuItem(
    uint16_t item_x, uint16_t item_y, const char* text, bool selected) {
  if (selected) {
    VDP_setTextPalette(PAL_YELLOW);
    VDP_drawText(">", item_x + MENU_SELECTOR_X_OFFSET, item_y);
  } else {
    VDP_setTextPalette(PAL_WHITE);
  }

  VDP_drawText(text, item_x, item_y);
}

void segavideo_menu_draw() {
  if (segavideo_getState() != Menu) {
    clearScreen();
    drawLogo();

    // TODO: Draw instructions

    segavideo_setState(Menu);
    menuChanged = true;
  }

  if (!menuChanged) return;

  for (int16_t offset = -1; offset <= 1; ++offset) {
    uint16_t menu_y = MENU_Y_OFFSET + (MENU_ITEM_Y_MULTIPLIER * offset);
    VDP_clearTextLine(menu_y);

    // Adding numVideos so the result is always positive.
    int index = (numVideos + selectedIndex + offset) % numVideos;
    drawMenuItem(MENU_ITEM_X, menu_y, menuLines[index],
                 /* selected= */ offset == 0);
  }

  // Draw thumbnail
  const uint8_t* data = KINETOSCOPE_MENU_DATA;
  const SegaVideoHeader* header = (const SegaVideoHeader*)data;
  header += selectedIndex;

  uint16_t tileIndex = 1;
  const uint16_t* tileMap = (const uint16_t*)(trivial_tilemap_half_0);
  uint16_t palNum = PAL_THUMB;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
  // Unpacked, raw pointer method used by VDP_loadTileSet
  VDP_loadTileData(header->thumbTiles, tileIndex, NUM_THUMB_TILES, CPU);

  // Unpacked, raw pointer method used by PAL_setPaletteColors
  PAL_setColors(palNum << 4, header->thumbPalette, /* count= */ 16, CPU);
#pragma GCC diagnostic pop

  // Unpacked, raw pointer method used by VDP_setTileMapEx
  VDP_setTileMapDataRectEx(BG_B, tileMap, tileIndex,
      /* x= */ THUMB_X, /* y= */ THUMB_Y,
      /* w= */ THUMB_MAP_W, /* h= */ THUMB_MAP_H,
      /* stride= */ THUMB_MAP_W, CPU);

  menuChanged = false;
}

void segavideo_menu_previousItem() {
  selectedIndex = (numVideos + selectedIndex - 1) % numVideos;
  menuChanged = true;
}

void segavideo_menu_nextItem() {
  selectedIndex = (selectedIndex + 1) % numVideos;
  menuChanged = true;
}

static void streamingLoopCallback() {
  // This is the streaming loop callback.  We need to initiate through the
  // hardware again, to get the right things back into SRAM.
 segavideo_menu_select(/* loop */ true);
}

static void streamingStopCallback() {
  // Playing from special hardware, so we should tell it to stop streaming.

  // We may have just sent CMD_FLIP_REGION without waiting.  Make sure we
  // have the token before sending a stop command.
  waitForReply(/* timeout_seconds= */ 1);

  uint16_t command_timeout = 30; // seconds
  if (!sendCommandAndWait(CMD_STOP_VIDEO, 0x00, command_timeout)) {
    errorMessage("Failed to stop video stream!");
  }
}

static void streamingFlipCallback() {
  // We send this command without awaiting a response.  Can't get stuck
  // waiting during playback.
  if (!sendCommand(CMD_FLIP_REGION, 0x00)) {
    errorMessage("Failed to flip region!");
  }
}

static void streamingEmuHackCallback() {
#if !defined(SIMULATE_HARDWARE)
  // HACK: Work around emulation issues.  Read the token so that the emulator
  // can check the time and execute a CMD_FLIP_REGION that was not awaited.
  volatile uint16_t* token_port = KINETOSCOPE_PORT_TOKEN;
  (void)*token_port;
#endif
}

bool segavideo_menu_select(bool loop) {
  uint16_t command_timeout = 30; // seconds
  uint16_t video_index = selectedIndex;
  if (!sendCommandAndWait(CMD_START_VIDEO, video_index, command_timeout)) {
    errorMessage("Failed to start video stream!");
    return false;
  }

  if (!segavideo_playInternal(KINETOSCOPE_VIDEO_DATA,
                              loop,
                              KINETOSCOPE_VIDEO_REGION_SIZE,
                              KINETOSCOPE_VIDEO_REGION_MASK,
                              streamingLoopCallback,
                              streamingStopCallback,
                              streamingFlipCallback,
                              streamingEmuHackCallback)) {
    errorMessage("Wrong video format!");
    return false;
  }
  return true;
}

bool segavideo_menu_hasError() {
  return pendingError() || segavideo_getState() == Error;
}

void segavideo_menu_showError() {
  if (segavideo_getState() != Error) {
    clearScreen();

    uint16_t command_timeout = 5; // seconds
    if (!sendCommandAndWait(CMD_GET_ERROR, 0, command_timeout)) {
      errorMessage("Failed to retrieve error!");
    } else {
      errorMessage(KINETOSCOPE_ERROR_DATA);
    }
  }
}

void segavideo_menu_clearError() {
  clearPendingError();
  segavideo_setState(Idle);
}
