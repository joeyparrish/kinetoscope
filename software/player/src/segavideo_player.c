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
#include "segavideo_state_internal.h"

#include "trivial_tilemap.h"

// State
static bool playing;
static bool paused;
static bool loop;
static const uint8_t* loopVideoData;

static SegaVideoChunkInfo currentChunk;
static SegaVideoChunkInfo nextChunk;
static uint32_t regionSize;
static uint32_t regionMask;
static VoidCallback* loopCallback;
static VoidCallback* stopCallback;
static VoidCallback* flipCallback;
static VoidCallback* emuHackCallback;

// Audio
static uint16_t sampleRate;
static uint32_t audioResumeAddr;
static uint32_t audioResumeSamples;

// Video
static uint16_t frameRate;
static uint16_t nextFrameNum;

// Hard-coded for now.  Fullscreen video only.
#define MAP_W 32
#define MAP_H 28
#define NUM_TILES (32 * 28)  // 896
#define FRAME_TILE_INDEX 0  // Overwrites 16 system tiles, but we need space

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

#if (DEBUG == 0)
# define kprintf(...)
#endif

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
  // Restore the first system tile, overwritten by playback.  This tile is used
  // to clear the screen.  This restore logic is adapted from SGDK's
  // VDP_resetScreen() in src/vdp.c.
  VDP_fillTileData(0, TILE_SYSTEM_INDEX, 1, TRUE);

  // Now clearing the screen should work as expected.
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
  uint16_t tileIndex = FRAME_TILE_INDEX + (second ? NUM_TILES : 0);

  // The order of loading things here matters, but it took some experimentation
  // to get it right.  Tiles, colors, then map gives us clean frames that look
  // good.  Tiles, map, then colors gives us some cruft on the first frame and
  // at potentially transitions.  Other orderings were super bad and crazy.

  // NOTE: We know our structures and their members are properly aligned in
  // reality, so we ignore this GCC warning here.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
  // Unpacked, raw pointer method used by VDP_loadTileSet
  VDP_loadTileData(frame->tiles, tileIndex, NUM_TILES, CPU);

  // Unpacked, raw pointer method used by PAL_setPaletteColors
  PAL_setColors(palNum << 4, frame->palette, /* count= */ 16, CPU);
#pragma GCC diagnostic pop

  // Unpacked, raw pointer method used by VDP_setTileMapEx
  VDP_setTileMapDataRectEx(BG_B, tileMap, tileIndex,
      /* x= */ 0, /* y= */ 0,
      /* w= */ MAP_W, /* h= */ MAP_H,
      /* stride= */ MAP_W, CPU);

  nextFrameNum = currentFrameNum + 1;

  emuHackCallback();

  // The logic here manages some tricky chunk transitions.  Note that if we
  // have a serious issue with frame-dropping, this whole thing falls apart.

  // Two frames before the end of the chunk, change the audio address.  When
  // the audio driver "loops", it will play the next chunk.  If we wait until
  // the last frame, it's too late, and the audio driver has already looped.
  bool changeAudioAddress =
      (nextFrameNum == currentChunk.numFrames - 2) ||
      (currentChunk.numFrames == 1);

  // After showing the last frame, change addresses to the next chunk.
  bool switchChunks = (nextFrameNum == currentChunk.numFrames);

  if (changeAudioAddress) {
    prepNextChunk(&currentChunk,
                  &nextChunk,
                  /* pointerBase= */ NULL,
                  regionMask,
                  regionSize);
    kprintf("Next audio buffer: %p (%d)\n",
            nextChunk.audioStart, (int)nextChunk.audioSamples);
    overwriteAudioAddress(nextChunk.audioStart, nextChunk.audioSamples);
  } else if (switchChunks) {
    nextFrameNum = 0;
    currentChunk = nextChunk;

    if (currentChunk.flipRegion) {
      flipCallback();
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
  XGM2_unloadDriver();

  paused = false;
  playing = false;
  loop = false;
  loopVideoData = NULL;
  segavideo_setState(Idle);
}

bool segavideo_playInternal(const uint8_t* videoData, bool pleaseLoop,
                            uint32_t pleaseRegionSize,
                            uint32_t pleaseRegionMask,
                            VoidCallback* pleaseLoopCallback,
                            VoidCallback* pleaseStopCallback,
                            VoidCallback* pleaseFlipCallback,
                            VoidCallback* pleaseEmuHackCallback) {
  regionSize = pleaseRegionSize;
  regionMask = pleaseRegionMask;
  loopCallback = pleaseLoopCallback;
  stopCallback = pleaseStopCallback;
  flipCallback = pleaseFlipCallback;
  emuHackCallback = pleaseEmuHackCallback;
  segavideo_setState(Player);

  if (!segavideo_validateHeader(videoData)) {
    return false;
  }
  const SegaVideoHeader* header = (const SegaVideoHeader*)videoData;

  // State
  paused = false;
  loop = pleaseLoop;
  playing = true;
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

static void doNothingCallback() {
  // Do nothing.
}

static void simpleLoopCallback() {
  // Only works to call it again after segavideo_stop().
  // This is the version for content built into a ROM.
  segavideo_playInternal(loopVideoData, /* loop= */ true,
                         /* region size */ 0, /* region mask */ 0,
                         simpleLoopCallback,
                         doNothingCallback,
                         doNothingCallback,
                         doNothingCallback);
}

void segavideo_play(const uint8_t* videoData, bool loop) {
  kprintf("segavideo_play\n");
  segavideo_playInternal(videoData, loop,
                         /* region size */ 0, /* region mask */ 0,
                         simpleLoopCallback,
                         doNothingCallback,
                         doNothingCallback,
                         doNothingCallback);
}

void segavideo_processFrames() {
  if (!playing || paused) return;

  bool stillPlaying = nextVideoFrame();
  if (segavideo_getState() == Error) {
    return;
  }

  if (!stillPlaying) {
    segavideo_stop();

    if (loop) {
      loopCallback();
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
  XGM2_unloadDriver();

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
    XGM2_unloadDriver();
  }

  // When we stop the video, clear the screen and load the default font, which
  // may have been overwritten by video playback.
  clearScreen();
  VDP_loadFont(&font_default, CPU);

  paused = false;
  playing = false;

  segavideo_setState(Idle);
  stopCallback();
}

bool segavideo_isPlaying() {
  return segavideo_getState() == Player && playing;
}
