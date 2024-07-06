// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Sega video parser routines, shared between projects.
//
// This can run on the Sega, inside an emulator, or in the firmware of the
// streaming hardware.

#ifndef _SEGAVIDEO_PARSER_H
#define _SEGAVIDEO_PARSER_H

#if defined(SGDK_GCC)
# include <genesis.h>
#else
# include <stdint.h>
# if !defined(__cplusplus)
  typedef uint8_t bool;
  static const bool false = 0;
  static const bool true = 1;
# endif
#endif

typedef struct SegaVideoChunkInfo {
  const uint8_t* start;
  const uint8_t* audioStart;
  uint32_t audioSamples;
  const uint8_t* frameStart;
  uint32_t numFrames;
  const uint8_t* end;
  bool flipRegion;
} SegaVideoChunkInfo;

bool segavideo_validateHeader(const uint8_t* videoData);

void segavideo_parseChunk(const uint8_t* chunkStart,
                          SegaVideoChunkInfo* chunkInfo);

#endif // _SEGAVIDEO_PARSER_H
