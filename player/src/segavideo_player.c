// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Sega video player and streaming hardware interface.

#include <genesis.h>

#include "segavideo_player.h"
#include "segavideo_parser.h"
#include "segavideo_format.h"
#include "trivial_tilemap.h"

// State
static bool playing;
static bool paused;
static bool loop;
static bool menuShowing;
static const uint8_t* loopVideoData;

static SegaVideoChunkInfo currentChunk;
static SegaVideoChunkInfo nextChunk;
static uint32_t regionSize;
static uint32_t regionMask;

// Audio
static uint16_t sampleRate;
static uint32_t audioResumeAddr;
static uint32_t audioResumeSamples;

// Video
static uint16_t frameRate;
static uint16_t nextFrameNum;

// Menu
static char **menuLines;
static uint16_t maxIndex;
static uint16_t selectedIndex;
#define MAX_MENU_SIZE 10
#define MENU_SELECTOR_X_OFFSET 1
#define MENU_X_OFFSET 3
#define MENU_Y_OFFSET 1

// Hard-coded for now.  Fullscreen video only.
#define MAP_W 32
#define MAP_H 28
#define NUM_TILES (32 * 28)
#define TILE_SIZE 32

// Ports to communicate with our special hardware.
#define VIDEOSTREAM_PORT_COMMAND (volatile uint16_t*)0xA13030
#define VIDEOSTREAM_PORT_TOKEN   (volatile uint16_t*)0xA13032
#define VIDEOSTREAM_PORT_ARG     (volatile uint16_t*)0xA13034
#define VIDEOSTREAM_DATA          (volatile uint8_t*)0x200000

// Commands for that hardware.
#define CMD_ECHO        0x00
#define CMD_LIST_VIDEOS 0x01
#define CMD_START_VIDEO 0x02
#define CMD_STOP_VIDEO  0x03
#define CMD_FLIP_REGION 0x04

// Token values for async communication.
#define TOKEN_CONTROL_TO_SEGA     0
#define TOKEN_CONTROL_TO_STREAMER 1

// Palettes used for on-screen text.
#define PAL_WHITE  PAL2
#define PAL_YELLOW PAL3

// NOTE: We use the XGM2 driver.  With the PCM-specific drivers, I found audio
// got "bubbly"-sounding during full-screen VDP tile transfers.  The XGM2
// driver does not suffer from this.  I noticed while reading its source that
// it disables interrupts while writing audio to the output.  This might
// explain it, but so long as we have one driver that works, it doesn't matter
// exactly what makes it different.  I'm not going to use a custom PCM driver
// with interrupts disabled if I can just use XGM2.

// NOTE:
// Looking for the XGM2 driver's PCM0_ADDR, I found:
//   PCM0_ADDR = PCM0_VARS + PCM_ADDR_OFF
//   PCM_ADDR_OFF = 0
//     PCM0_ADDR = PCM0_VARS
//   PCM0_VARS = PCM_VARS + 0
//     PCM0_ADDR = PCM_VARS
//   PCM_VARS = VARS + 0xD0
//     PCM0_ADDR = VARS + 0xD0
//   VARS = 0x0110
//     PCM0_ADDR = 0x0110 + 0xD0
//     PCM0_ADDR = 0x01E0
// Looking for the XGM2 driver's PCM0_ADDR_ARG, I found:
//   PCM0_ADDR_ARG = SID_TABLE + 0x01F4
//   SID_TABLE = SID_TABLE_H << 8
//     PCM0_ADDR_ARG = (SID_TABLE_H << 8) + 0x01F4
//   SID_TABLE_H = 0x1C
//     PCM0_ADDR_ARG = 0x1C00 + 0x01F4
//     PCM0_ADDR_ARG = 0x1DF4
// The C interface does not expose these addresses, so we hard-code them below.
// Note also that these addresses all apply only to SOUND_PCM_CH1.

// At this address, the first three bytes are the current PCM address, but we
// only use the middle and high bytes.  The next two bytes are the remaining
// PCM length divided by 64, in bytes.  The next byte is flags, including the
// loop bit 0x80.
#define XGM2_CURRENT ((volatile uint8_t*)(Z80_RAM + 0x01E0))

// At this address, the first two bytes are the base PCM address divided by 256.
// The next two bytes are the PCM length divided by 64, in bytes.
#define XGM2_PARAMS ((volatile uint8_t*)(Z80_RAM + 0x1DF4))

// The status byte will have bit 0x01 set if we are playing PCM channel 1.
#define XGM2_STATUS ((volatile uint8_t*)(Z80_RAM + 0x0102))

// Offset a pointer against a base pointer
#define OFFSET(pointer, base) \
    (((uint32_t)pointer) - ((uint32_t)base))

// Mask a pointer offset as a uint32_t
#define MASK(pointer, base, mask) (OFFSET(pointer, base) & mask)

// Turn an offset and base pointer back into a real pointer
#define OFFSET_TO_POINTER(offset, base) \
    ((uint8_t*)(((uint32_t)offset) + ((uint32_t)base)))

// Forward declarations:
static bool sendCommand(uint16_t command, uint16_t arg0);
static bool waitForReply(uint16_t timeout_seconds);
static bool sendCommandAndWait(
    uint16_t command, uint16_t arg0, uint16_t timeout_seconds);
static void statusMessage(uint16_t pal, const char* message);

static void prepNextChunk(const SegaVideoChunkInfo* currentChunk,
                          SegaVideoChunkInfo* nextChunk,
                          uint8_t* pointerBase,
                          uint32_t regionMask,
                          uint32_t regionSize) {
  kprintf("Current chunk: %p => %p\n", currentChunk->start, currentChunk->end);

  // Compute chunk placement
  const uint8_t* chunkStart = currentChunk->end;
  const uint32_t chunkSize =
      ((uint32_t)currentChunk->end) - ((uint32_t)currentChunk->start);
  const uint8_t* estimatedChunkEnd = currentChunk->end + chunkSize;
  bool flipRegion = false;

  if (MASK(estimatedChunkEnd - 1, pointerBase, regionMask) !=
      MASK(chunkStart, pointerBase, regionMask)) {
    // The next chunk as computed would cross a boundary in SRAM, so the
    // streamer hardware will instead place it at the start of the other
    // region.
    chunkStart = OFFSET_TO_POINTER(
        MASK(chunkStart, pointerBase, regionMask) ^ regionSize,
        pointerBase);
    flipRegion = true;
    kprintf("Next chunk wrapped: %p\n", chunkStart);
  }

  segavideo_parseChunk(chunkStart, nextChunk);
  nextChunk->flipRegion = flipRegion;
  kprintf("Next chunk: %p => %p\n", nextChunk->start, nextChunk->end);

  if (!nextChunk->audioSamples || !nextChunk->numFrames) {
    kprintf("No more chunks!\n");
    nextChunk->audioStart = NULL;
    nextChunk->frameStart = NULL;
    nextChunk->flipRegion = false;
  }
}

static bool regionOverflow(const SegaVideoChunkInfo* chunk,
                           uint8_t* pointerBase,
                           uint32_t regionMask) {
  uint32_t startRegion = MASK(chunk->start, pointerBase, regionMask);
  uint32_t endRegion = MASK(chunk->end - 1, pointerBase, regionMask);
  return startRegion != endRegion;
}

static void clearScreen() {
  VDP_clearPlane(BG_B, /* wait= */ true);
}

static void overwriteAudioAddress(const uint8_t* samples, uint32_t length) {
  Z80_requestBus(true);

  if (samples) {
    // Next address to loop back to.
    XGM2_PARAMS[0] = ((uint32_t)samples) >> 8;
    XGM2_PARAMS[1] = ((uint32_t)samples) >> 16;
    XGM2_PARAMS[2] = length >> 6;
    XGM2_PARAMS[3] = length >> 14;
  } else {
    // All out of data, so disable the "loop" flag.  Playback will end when the
    // current buffer runs out.
    XGM2_CURRENT[5] = 0;
  }

  Z80_releaseBus();
}

static uint32_t getCurrentAudioAddress() {
  Z80_requestBus(true);

  uint8_t addrMid = 0;
  uint8_t addrHigh = 0;

  if (*XGM2_STATUS & 1) {
    // Something is playing.
    // XGM2_CURRENT[0] is addrLow and is not used.
    addrMid = XGM2_CURRENT[1];
    addrHigh = XGM2_CURRENT[2];
  } else {
    // Leave the address at 0, to represent that we are not playing.
  }

  Z80_releaseBus();

  return (addrMid << 8) | (addrHigh << 16);
}

static bool nextVideoFrame() {
  // Get the current audio address to sync video frames against.
  uint32_t currentSample = getCurrentAudioAddress();

  // Nothing to play.  Audio has stopped.
  if (!currentSample) {
    kprintf("EOF!\n");
    return false;
  }

  // No more frames, but we let the audio finish playing, so return true.
  if (!currentChunk.numFrames) return true;

  // Compute frame timing.
  uint32_t firstSample = (uint32_t)currentChunk.audioStart;
  uint32_t samplesPlayed = (currentSample - firstSample);

  // The calculation below must not overflow.  In a ROM, this is not a concern,
  // since you can't fit more than about 120 video frames in a 4MB ROM.
  // In a streaming scenario, where ROM size isn't an issue, we need to
  // consider the limit of this calculation.  (samplesPlayed * frameRate) must
  // be less than 1<<32 to avoid overflow.  At a frameRate of 10 fps and a
  // sample rate of 13312 Hz, this overflows after 538 minutes, which is nearly
  // 9 hours.
  uint32_t currentFrameNum = samplesPlayed * frameRate / sampleRate;

  // Not yet time for a new frame.
  if (currentFrameNum < nextFrameNum) return true;

  // Debug dropped frames:
  if (currentFrameNum != nextFrameNum) {
    kprintf("WARNING: FRAME DROPPED %d => %d\n",
        (int)nextFrameNum, (int)currentFrameNum);
  }

  const SegaVideoFrame* frame = (const SegaVideoFrame*)
      (currentChunk.frameStart + (sizeof(SegaVideoFrame) * currentFrameNum));

  // We alternate tile and palette indexes every frame.
  bool second = currentFrameNum & 1;
  const uint16_t* tileMap = (const uint16_t*)(
      second ? trivial_tilemap_1 : trivial_tilemap_0);
  uint16_t palNum =
      (tileMap[0] & TILE_ATTR_PALETTE_MASK) >> TILE_ATTR_PALETTE_SFT;
  // NOTE: We are hijacking system tiles for more space!
  // User tiles start at index 256, and the max index is 1425.
  uint16_t tileIndex = second ? NUM_TILES : 0;

  // Unpacked, raw pointer method used by VDP_loadTileSet
  VDP_loadTileData(frame->tiles, tileIndex, NUM_TILES, CPU);

  // Unpacked, raw pointer method used by VDP_setTileMapEx
  VDP_setTileMapDataRectEx(BG_B, tileMap, tileIndex,
      /* x= */ 0, /* y= */ 0,
      /* w= */ MAP_W, /* h= */ MAP_H,
      /* stride= */ MAP_W, CPU);

  // Unpacked, raw pointer method used by PAL_setPaletteColors
  PAL_setColors(palNum << 4, frame->palette, /* count= */ 16, CPU);

  nextFrameNum = currentFrameNum + 1;

  // HACK: Work around emulation issues by reading the token to trigger the
  // videostream emulator to check the time and execute CMD_FLIP_REGION that
  // was not awaited.
  volatile uint16_t* token_port = VIDEOSTREAM_PORT_TOKEN;
  (void)*token_port;

  // The logic here manages some tricky chunk transitions.  Note that if we
  // have a serious issue with frame-dropping, this whole thing falls apart.
  if (nextFrameNum == currentChunk.numFrames - 1) {
    // One frame before the end of the chunk, change the audio address.  When
    // the audio driver "loops", it will play the next chunk.  If we wait until
    // the last frame, it's too late, and the audio driver has already looped.
    prepNextChunk(&currentChunk,
                  &nextChunk,
                  /* pointerBase= */ NULL,
                  regionMask,
                  regionSize);
    kprintf("Next audio buffer: %p (%d)\n",
            nextChunk.audioStart, (int)nextChunk.audioSamples);
    overwriteAudioAddress(nextChunk.audioStart, nextChunk.audioSamples);
  } else if (nextFrameNum == currentChunk.numFrames) {
    // After showing the last frame, change addressess to the next chunk.
    nextFrameNum = 0;
    currentChunk = nextChunk;

    if (currentChunk.flipRegion) {
      // We send this command without awaiting a response.  Can't get stuck
      // waiting during playback.
      if (!sendCommand(CMD_FLIP_REGION, 0x00)) {
        statusMessage(PAL_YELLOW, "Failed to request filling of next region!");
        waitMs(3000);
        return false;
      }
    }
  }

  return true;
}

void segavideo_init() {
  kprintf("segavideo_init\n");

  // Narrow screen.  This saves us VRAM for tiles, so we can have two full
  // frames of video in VRAM at once.  We make up for it in the encoding of the
  // tiles, so the video looks right.
  VDP_setScreenWidth256();
  VDP_setScreenHeight224();
  VDP_setPlaneSize(32, 32, true);

  // Move BGA and BGB both to 0xE000 (BGA default) to make room for tiles.
  // Also move window to the same.  This makes window unusable.
  VDP_setBGBAddress(0xE000);
  VDP_setBGAAddress(0xE000);
  VDP_setWindowAddress(0xE000);

  // Unload any previous audio driver for a clean slate.
  Z80_unloadDriver();

  // Load menu colors.
  uint16_t white  = 0x0FFF;  // ABGR
  uint16_t yellow = 0x00FF;  // ABGR
  // Text uses the last entry of each palette.
  PAL_setColors(PAL_WHITE  * 16 + 15, &white,  /* count= */ 1, CPU);
  PAL_setColors(PAL_YELLOW * 16 + 15, &yellow, /* count= */ 1, CPU);

  // Allocate menu line pointers.
  menuLines = (char**)MEM_alloc(sizeof(char*) * MAX_MENU_SIZE);
  // Allocate a full row of text for each line.
  for (uint16_t i = 0; i < MAX_MENU_SIZE; ++i) {
    // One extra for nul terminator:
    menuLines[i] = (char*)MEM_alloc(MAP_W + 1);
    memset(menuLines[i], 0, MAP_W + 1);
  }
  selectedIndex = 0;
  maxIndex = 0;

  paused = false;
  playing = false;
  loop = false;
  menuShowing = false;
  loopVideoData = NULL;
}

// Assumes that regionSize and regionMask have been set.
static bool segavideo_playInternal(const uint8_t* videoData, bool pleaseLoop) {
  // FIXME: garbage in first frame?  more visible on loop.

  if (!segavideo_validateHeader(videoData)) {
    return false;
  }
  const SegaVideoHeader* header = (const SegaVideoHeader*)videoData;

  // State
  paused = false;
  loop = pleaseLoop;
  playing = true;
  menuShowing = false;
  loopVideoData = videoData;

  // Audio
  sampleRate = header->sampleRate;
  audioResumeAddr = 0;
  audioResumeSamples = 0;

  // Video
  frameRate = header->frameRate;
  nextFrameNum = 0;

  // Parse chunk header
  const uint8_t* chunkStart = videoData + sizeof(SegaVideoHeader);
  segavideo_parseChunk(chunkStart, &currentChunk);
  currentChunk.flipRegion = false;

  if (regionOverflow(&currentChunk,
                     /* pointerBase= */ NULL,
                     regionMask)) {
    kprintf("First video chunk overflows the region!\n");
    return false;
  }

  // Clear anything that might have been on screen before.
  clearScreen();

  // Start audio
  if (currentChunk.audioSamples) {
    XGM2_loadDriver(true);

    XGM2_playPCMEx(currentChunk.audioStart, currentChunk.audioSamples,
        SOUND_PCM_CH1, /*priority=*/ 6, /*halfRate=*/ false, /*loop=*/ true);

    while (!Z80_isDriverReady()) {
      waitMs(1);
    }
    while (!XGM2_isPlayingPCM(SOUND_PCM_CH1_MSK)) {
      waitMs(1);
    }
  }

  return true;
}

void segavideo_play(const uint8_t* videoData, bool loop) {
  kprintf("segavideo_play\n");
  // Continuous play from ROM, no regions or SRAM magic.
  regionSize = 0;
  regionMask = 0;
  segavideo_playInternal(videoData, loop);
}

void segavideo_processFrames() {
  if (!playing || paused) return;

  bool stillPlaying = nextVideoFrame();
  if (!stillPlaying) {
    segavideo_stop();

    if (loop) {
      if (regionMask == 0) {
        // Only works to call it again after segavideo_stop().
        // This is the version for content built into a ROM.
        segavideo_playInternal(loopVideoData, /* loop= */ true);
      } else {
        // This is the streaming version.  We need to initiate through the
        // hardware again, to get the right things back into SRAM.
        segavideo_stream(true);
      }
    }
  }
}

void segavideo_pause() {
  kprintf("segavideo_pause\n");

  if (!playing || paused) {
    return;
  }

  // Grab current audio address and length from the driver to resume later.
  audioResumeAddr = getCurrentAudioAddress();

  // The length stored in XGM2_CURRENT is a multiple of 64, not 256 as required
  // by XGM2_playPCMEx.  Therefore we don't read it.  We just compute the
  // length based on resumeAddr, which is properly aligned to 256 bytes.
  audioResumeSamples = (uint32_t)currentChunk.audioStart
                       + currentChunk.audioSamples
                       - audioResumeAddr;

  // Stop audio.
  XGM2_stopPCM(SOUND_PCM_CH1);

  // NOTE: XGM2_stopPCM() followed by XGM2_playPCMEx() doesn't work without
  // unloading and reloading the driver.  This is likely a bug in the driver,
  // but I don't have time to dig into it.
  Z80_unloadDriver();

  // Disable video
  paused = true;
}

void segavideo_resume() {
  kprintf("segavideo_resume\n");

  if (!playing || !paused) {
    return;
  }

  XGM2_loadDriver(true);
  XGM2_playPCMEx((uint8_t*)audioResumeAddr, audioResumeSamples, SOUND_PCM_CH1,
      /*priority=*/ 6, /*halfRate=*/ false, /*loop=*/ true);

  // Wait for the "playing" signal so we know the internal addresses have been
  // set.
  while (!Z80_isDriverReady()) {
    waitMs(1);
  }
  while (!XGM2_isPlayingPCM(SOUND_PCM_CH1_MSK)) {
    waitMs(1);
  }

  // Enable video
  paused = false;

  // The driver has now copied the addresses into its internal memory.
  // Overwrite the starting address so that if we loop, it starts at the
  // beginning, not this resume point.
  overwriteAudioAddress(currentChunk.audioStart, currentChunk.audioSamples);
}

void segavideo_togglePause() {
  if (paused) {
    segavideo_resume();
  } else {
    segavideo_pause();
  }
}

void segavideo_stop() {
  kprintf("segavideo_stop\n");

  if (playing) {
    // Stop audio.
    XGM2_stopPCM(SOUND_PCM_CH1);
    Z80_unloadDriver();
  }

  if (regionSize) {
    // Playing from special hardware, so we should tell it to stop streaming.

    // We may have just sent CMD_FLIP_REGION without waiting.  Make sure we
    // have the token before sending a stop command.
    waitForReply(/* timeout_seconds= */ 1);

    uint16_t command_timeout = 30; // seconds
    if (!sendCommandAndWait(CMD_STOP_VIDEO, 0x00, command_timeout)) {
      statusMessage(PAL_YELLOW, "Failed to stop video stream!");
      waitMs(3000);
    }
  }

  paused = false;
  playing = false;
  menuShowing = false;
}

bool segavideo_isPlaying() {
  return playing;
}

bool segavideo_isMenuShowing() {
  return menuShowing;
}

static void statusMessage(uint16_t pal, const char* message) {
  clearScreen();
  VDP_setTextPalette(pal);
  VDP_drawText(message, 1, 1);
  kprintf("%s\n", message);
}

static bool sendCommand(uint16_t command, uint16_t arg0) {
  volatile uint16_t* command_port = VIDEOSTREAM_PORT_COMMAND;
  volatile uint16_t* token_port = VIDEOSTREAM_PORT_TOKEN;
  volatile uint16_t* arg_port = VIDEOSTREAM_PORT_ARG;

  if (*token_port != TOKEN_CONTROL_TO_SEGA) {
    return false;
  }

  *command_port = command;
  *arg_port = arg0;
  *token_port = TOKEN_CONTROL_TO_STREAMER;
  return true;
}

static bool waitForReply(uint16_t timeout_seconds) {
  kprintf("Waiting for streamer response.\n");

  volatile uint16_t* token_port = VIDEOSTREAM_PORT_TOKEN;
  uint16_t counter = 0;
  uint16_t max_counter =
      IS_PAL_SYSTEM ? 50 * timeout_seconds : 60 * timeout_seconds;
  while (*token_port == TOKEN_CONTROL_TO_STREAMER && ++counter < max_counter) {
    SYS_doVBlankProcess();
  }

  if (*token_port == TOKEN_CONTROL_TO_STREAMER) {
    return false;
  }

  return true;
}

static bool sendCommandAndWait(
    uint16_t command, uint16_t arg0, uint16_t timeout_seconds) {
  return sendCommand(command, arg0) && waitForReply(timeout_seconds);
}

bool segavideo_checkHardware() {
  statusMessage(PAL_WHITE, "Checking for streamer...");

  uint16_t command_timeout = 5; // seconds
  volatile uint8_t* data = VIDEOSTREAM_DATA;

  if (!sendCommand(CMD_ECHO, 0x55)) {
    statusMessage(PAL_YELLOW, "Streamer not found! (code 1)");
    kprintf("The token was in an invalid state. Streamer hardware unlikely.\n");
    return false;
  }

  if (!waitForReply(command_timeout)) {
    statusMessage(PAL_YELLOW, "Streamer not found! (code 2)");
    kprintf("No reply from streamer hardware before timeout.\n");
    return false;
  }

  if (*data != 0x55) {
    statusMessage(PAL_YELLOW, "Streamer not found! (code 3)");
    kprintf("Unable to find 0x55 echoed back: %d\n", *data);
    return false;
  }

  if (!sendCommandAndWait(CMD_ECHO, 0xAA, command_timeout)) {
    statusMessage(PAL_YELLOW, "Streamer not found! (code 4)");
    return false;
  }

  if (*data != 0xAA) {
    statusMessage(PAL_YELLOW, "Streamer not found! (code 5)");
    kprintf("Unable to find 0xAA echoed back: %d\n", *data);
    return false;
  }

  statusMessage(PAL_WHITE, "Streamer found!");
  waitMs(1000);
  return true;
}

bool segavideo_getMenu() {
  statusMessage(PAL_WHITE, "Fetching video list...");

  uint16_t command_timeout = 30; // seconds
  volatile uint8_t* data = VIDEOSTREAM_DATA;

  if (!sendCommandAndWait(CMD_LIST_VIDEOS, 0x00, command_timeout)) {
    statusMessage(PAL_YELLOW, "Failed to fetch video list!");
    return false;
  }

  maxIndex = 0;
  selectedIndex = 0;

  // Split the output at newlines and display one item per line.  The lines are
  // copied into CPU memory so that we can redraw the screen as the user
  // navigates.
  // TODO: Handle multiple pages of results.
  uint16_t index = 0;
  while (*data && index < MAX_MENU_SIZE) {
    // Fill in the title.
    uint16_t bytes_left = MAP_W;
    char* next_output = menuLines[index];
    while (*data && *data != '\n') {
      // Ignore any excess bytes that don't fit in the row.
      if (bytes_left) {
        *next_output = *data;
        next_output++;
        bytes_left--;
      }
      data++;
    }

    // We left room for the nul terminator, since we allocated MAP_W+1.
    *next_output = '\0';

    // Move to the next menu line.
    index++;

    if (*data == '\n') {
      // Advance to the next line of input data.
      data++;
    }
  }

  maxIndex = index;

  return true;
}

void segavideo_drawMenu() {
  menuShowing = true;

  for (uint16_t index = 0; index < MAX_MENU_SIZE; ++index) {
    VDP_clearTextLine(index + MENU_Y_OFFSET);

    if (index > maxIndex) continue;

    if (index == selectedIndex) {
      VDP_setTextPalette(PAL_YELLOW);
      VDP_drawText(">", /* x= */ MENU_SELECTOR_X_OFFSET,
                   /* y= */ index + MENU_Y_OFFSET);
    } else {
      VDP_setTextPalette(PAL_WHITE);
    }

    VDP_drawText(menuLines[index], /* x= */ MENU_X_OFFSET,
                 /* y= */ index + MENU_Y_OFFSET);
  }
}

void segavideo_menuPreviousItem() {
  if (selectedIndex == 0) {
    selectedIndex = maxIndex - 1;
  } else {
    selectedIndex--;
  }
}

void segavideo_menuNextItem() {
  selectedIndex++;
  if (selectedIndex >= maxIndex) {
    selectedIndex = 0;
  }
}

bool segavideo_stream(bool loop) {
  menuShowing = false;

  uint16_t command_timeout = 30; // seconds
  if (!sendCommandAndWait(CMD_START_VIDEO, selectedIndex, command_timeout)) {
    statusMessage(PAL_YELLOW, "Failed to start video stream!");
    waitMs(3000);
    return false;
  }

  // Play from two SRAM regions:
  //  - starting at 0x200000 and ending at 0x300000
  //  - starting at 0x300000 and ending at 0x400000
  // The streamer hardware will fill in whole chunks only into these regions,
  // flipping back and forth between them.
  const uint8_t* videoData  = (uint8_t*)0x200000;
  regionSize                =           0x100000;  // 1MB
  regionMask                =           0x300000;
  if (!segavideo_playInternal(videoData, loop)) {
    statusMessage(PAL_YELLOW, "Wrong video format!");
    waitMs(3000);
    return false;
  }
  return true;
}
