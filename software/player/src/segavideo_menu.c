// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Sega menu interface.

#include <genesis.h>
#include <string.h>

#include "segavideo_menu.h"
#include "segavideo_format.h"
#include "segavideo_player.h"
#include "segavideo_state_internal.h"

#include "kinetoscope_logo.h"
#include "menu_font.h"
#include "trivial_tilemap.h"

//#define DEBUG 1

static bool menuChanged;
static char **menuLines;
static int numVideos;
static int selectedIndex;
static int max_status_y = 0;

// All offsets and sizes are in tiles, not pixels
#define MENU_ITEM_X 2
#define MENU_ITEM_Y_MULTIPLIER 2
#define MENU_Y_OFFSET 9
#define MENU_SELECTOR_X_OFFSET -2

#define STATUS_MESSAGE_X 1
#define STATUS_MESSAGE_Y 7
#define STATUS_MESSAGE_W 30

#define THUMB_X 15
#define THUMB_Y 13
#define THUMB_MAP_W 16
#define THUMB_MAP_H 14
#define THUMB_TILES (16 * 14)  // 224
#define THUMB_TILE_INDEX 1
#define THUMB_TILE_INDEX_2 (THUMB_TILE_INDEX + THUMB_TILES)

#define LOGO_X 2
#define LOGO_Y 1
#define LOGO_TILE_INDEX (THUMB_TILE_INDEX_2 + THUMB_TILES)
#define LOGO_TILES (28 * 6)  // 168

#define INSTRUCTIONS_X 0
#define INSTRUCTIONS_Y 15

// NOTE: The font occupies 96 tiles, 1696 through 1791

#define MAX_CATALOG_SIZE 127

#if defined(SIMULATE_HARDWARE)
# include "embedded_catalog.h"
# include "embedded_video.h"

# define KINETOSCOPE_MENU_DATA embedded_catalog
# define KINETOSCOPE_ERROR_DATA "Error: something went wrong!"
# define KINETOSCOPE_VIDEO_DATA embedded_video
# define KINETOSCOPE_VIDEO_REGION_SIZE 0
# define KINETOSCOPE_VIDEO_REGION_MASK 0xffffffff
#else
// Ports to communicate with our special hardware.
# define KINETOSCOPE_PORT_COMMAND ((volatile uint16_t*)0xA13010)  // low 8 bits
# define KINETOSCOPE_PORT_ARG     ((volatile uint16_t*)0xA13012)  // low 8 bits
# define KINETOSCOPE_PORT_TOKEN   ((volatile uint16_t*)0xA13008)  // low 1 bit, set on write
# define KINETOSCOPE_PORT_ERROR   ((volatile uint16_t*)0xA1300A)  // low 1 bit, clear on write
# define KINETOSCOPE_DATA          ((volatile uint8_t*)0x200000)
# define KINETOSCOPE_MENU_DATA        ((const uint8_t*)KINETOSCOPE_DATA)
# define KINETOSCOPE_ERROR_DATA          ((const char*)KINETOSCOPE_DATA)

// Play from two SRAM regions:
//  - starting at 0x200000 and ending at 0x300000
//  - starting at 0x300000 and ending at 0x400000
// The streamer hardware will fill in whole chunks only into these regions,
// flipping back and forth between them.
# define KINETOSCOPE_VIDEO_DATA       ((const uint8_t*)KINETOSCOPE_DATA)
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
#define CMD_CONNECT_NET 0x06  // Connect to the network

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

static void drawMultilineText(const char* text) {
  const int max_line_len = STATUS_MESSAGE_W;
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

  int new_max_status_y = y;

  // Clear remaining lines of the old status message.
  while (y < max_status_y) {
    VDP_clearTextArea(STATUS_MESSAGE_X, y, max_line_len, 1);
    y++;
  }
  max_status_y = new_max_status_y;
}

static void genericMessage(uint16_t pal, const char* message) {
  // Load the menu font (which may have been overwritten by video playback).
  VDP_loadFont(&menu_font, CPU);

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


#define ERROR_MAX 256
static char error_message_buffer[ERROR_MAX];

// SGDK has no snprintf.  God help you if you write more than ERROR_MAX
// characters...
static void errorMessage(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vsprintf(error_message_buffer, format, args);
  va_end(args);

  if (!segavideo_menu_hasError()) {
    genericMessage(PAL_YELLOW, error_message_buffer);
    segavideo_setState(Error);
  }
}

static bool pendingError() {
#if defined(SIMULATE_HARDWARE)
  return false;
#else
  // NOTE: Only bit zero is meaningful.  The others are all garbage.
  return ((*KINETOSCOPE_PORT_ERROR) & 1) != 0;
#endif
}

static void clearPendingError() {
#if !defined(SIMULATE_HARDWARE)
  // NOTE: The data doesn't matter in hardware.  Any write will clear this.
  *KINETOSCOPE_PORT_ERROR = 0;
#endif
}

static bool isSegaInControl() {
#if defined(SIMULATE_HARDWARE)
  return true;
#else
  // NOTE: Only bit zero is meaningful.  The others are all garbage.
  return ((*KINETOSCOPE_PORT_TOKEN) & 1) == TOKEN_CONTROL_TO_SEGA;
#endif
}

static void passControlToStreamer() {
#if !defined(SIMULATE_HARDWARE)
  // NOTE: The data doesn't matter in hardware.  Any write will clear this.
  *KINETOSCOPE_PORT_TOKEN = TOKEN_CONTROL_TO_STREAMER;
#endif
}

static void writeCommand(uint16_t command, uint16_t arg0) {
#if !defined(SIMULATE_HARDWARE)
  *KINETOSCOPE_PORT_COMMAND = command;
  *KINETOSCOPE_PORT_ARG = arg0;
#endif
}

static bool sendCommand(uint16_t command, uint16_t arg0) {
  if (!isSegaInControl()) {
    return false;
  }

  writeCommand(command, arg0);
  passControlToStreamer();
  return true;
}

static bool waitForReply(uint16_t timeout_seconds) {
  kprintf("Waiting for streamer response.\n");

  uint16_t counter = 0;
  uint16_t max_counter =
      IS_PAL_SYSTEM ? 50 * timeout_seconds : 60 * timeout_seconds;
  while (!isSegaInControl() && ++counter < max_counter) {
    SYS_doVBlankProcess();
  }

  if (!isSegaInControl()) {
    return false;
  }

  return true;
}

static bool sendCommandAndWait(
    uint16_t command, uint16_t arg0, uint16_t timeout_seconds) {
  return sendCommand(command, arg0) && waitForReply(timeout_seconds);
}

static void clearScreen() {
  VDP_clearPlane(BG_B, /* wait= */ true);
}

static void loadMenuColors() {
  // Load menu colors.
  uint16_t white  = 0x0FFF;  // ABGR
  uint16_t yellow = 0x00FF;  // ABGR
  // The custom font uses the first entry of each palette.
  PAL_setColors(PAL_WHITE  * 16 + 1, &white,  /* count= */ 1, CPU);
  PAL_setColors(PAL_YELLOW * 16 + 1, &yellow, /* count= */ 1, CPU);
}

static void drawLogo() {
  VDP_drawImageEx(
      BG_B, &kinetoscope_logo,
      TILE_ATTR_FULL(PAL_LOGO, FALSE, FALSE, FALSE, LOGO_TILE_INDEX),
      LOGO_X, LOGO_Y,
      /* load palette */ TRUE,
      CPU);

  // It's not clear to me what is overwriting these palettes, but reloading at
  // this point fixes them.
  loadMenuColors();
}

void segavideo_menu_init() {
  kprintf("segavideo_menu_init\n");

  loadMenuColors();
  clearScreen();
  drawLogo();
  VDP_loadFont(&menu_font, CPU);

  // Allocate menu line pointers.
  menuLines = (char**)MEM_alloc(sizeof(char*) * MAX_CATALOG_SIZE);
  // Allocate a full row of text for each line.
  for (uint16_t i = 0; i < MAX_CATALOG_SIZE; ++i) {
    // One extra for nul terminator:
    menuLines[i] = (char*)MEM_alloc(STATUS_MESSAGE_W + 1);
    memset(menuLines[i], 0, STATUS_MESSAGE_W + 1);
  }

  selectedIndex = 0;
  numVideos = 0;
  menuChanged = false;
}

static void drawInstructions() {
  VDP_drawText("Choose a video", INSTRUCTIONS_X, INSTRUCTIONS_Y);
  VDP_drawText(" Press start ", INSTRUCTIONS_X, INSTRUCTIONS_Y + 2);
  VDP_drawText("   to play   ", INSTRUCTIONS_X, INSTRUCTIONS_Y + 3);
}

bool segavideo_menu_checkHardware() {
  clearScreen();
  drawLogo();

  statusMessage("Checking for Kinetoscope cartridge...");

#if defined(SIMULATE_HARDWARE)
  waitMs(1000);
  statusMessage("Simulated Kinetoscope cartridge detected!");
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
    errorMessage("Kinetoscope cartridge not found! (code 3, data 0x%02x)",
                 *data);
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

  statusMessage("Kinetoscope cartridge detected!");
#endif

  clearPendingError();
  waitMs(1000);
  return true;
}

bool segavideo_menu_load() {
  clearScreen();
  drawLogo();

  statusMessage("Connecting to the network...");
#if defined(SIMULATE_HARDWARE)
  waitMs(3000);
#endif
  uint16_t command_timeout = 40; // seconds
  if (!sendCommandAndWait(CMD_CONNECT_NET, 0, command_timeout)) {
    return false;
  }

  statusMessage("Fetching video list...");
#if defined(SIMULATE_HARDWARE)
  waitMs(3000);
#endif
  command_timeout = 30; // seconds
  if (!sendCommandAndWait(CMD_LIST_VIDEOS, 0, command_timeout)) {
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
    // and zero-padded.
    memcpy(menuLines[numVideos], header->title, STATUS_MESSAGE_W);
    menuLines[numVideos][STATUS_MESSAGE_W] = '\0';  // Allocated byte for this

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
    drawInstructions();
    drawLogo();

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

  bool second = selectedIndex & 1;
  uint16_t tileIndex = second ? THUMB_TILE_INDEX_2 : THUMB_TILE_INDEX;
  const uint16_t* tileMap = (const uint16_t*)(trivial_tilemap_half_0);
  uint16_t palNum = PAL_THUMB;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
  // Unpacked, raw pointer method used by VDP_loadTileSet
  VDP_loadTileData(header->thumbTiles, tileIndex, THUMB_TILES, CPU);

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
  return segavideo_getState() == Error || pendingError();
}

void segavideo_menu_showError() {
  if (segavideo_getState() != Error) {
    clearScreen();
    drawLogo();

    uint16_t command_timeout = 5; // seconds
    // NOTE: Bypassing errorMessage() here and calling genericMessage and
    // segavideo_setState to ensure we don't get locked out by pendingError()
    // and segavideo_menu_hasError().
    if (!sendCommandAndWait(CMD_GET_ERROR, 0, command_timeout)) {
      genericMessage(PAL_YELLOW, "Failed to retrieve error!");
    } else {
      genericMessage(PAL_YELLOW, KINETOSCOPE_ERROR_DATA);
    }
    segavideo_setState(Error);
  }
}

void segavideo_menu_clearError() {
  clearPendingError();
  segavideo_setState(Idle);
}
