// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Sega video format details.
// Used by the parser to interpret the file format.
//
// This can run on the Sega, inside an emulator, or in the firmware of the
// streaming hardware.

#ifndef _SEGAVIDEO_FORMAT_H
#define _SEGAVIDEO_FORMAT_H

#if defined(SGDK_GCC)
# include <genesis.h>
#else
# include <stdint.h>
#endif

#define SEGAVIDEO_HEADER_MAGIC  "what nintendon't"
#define SEGAVIDEO_HEADER_FORMAT 0x0002

// This header appears at the start of the file in both embedded and streaming
// mode.  Each one is exactly 8kB, so they can form the basis of a catalog
// format that the streamer ROM can easily flip through.  The catalog is the
// concatenation of the headers of all the videos, with the relative_url field
// filled in.  The relative_url field is not used in the video itself, only in
// the catalog.
typedef struct SegaVideoHeader {
  uint8_t magic[16];  // SEGAVIDEO_HEADER_MAGIC
  uint16_t format;  // SEGAVIDEO_HEADER_FORMAT
  uint16_t frameRate;  // fps
  uint16_t sampleRate;  // Hz
  uint32_t totalFrames;  // num frames
  uint32_t totalSamples;  // bytes, total, multiple of 256
  uint32_t chunkSize;  // bytes, for all but the final chunk
  uint32_t totalChunks;  // number of chunks to follow this header

  // 38 bytes above.
  char title[128];  // US-ASCII for display with a very simple font
  char relative_url[128];  // relative to catalog, filled in catalog creation
  uint8_t padding[698];  // zeros
  // 7200 bytes below.

  // A thumbnail for display in the streamer ROM menu. Just like
  // SegaVideoFrame below, but at 1/2 resolution in each dimension (16x14
  // tiles). See SegaVideoFrame for more details. Uses trivial_tilemap_half_0.
  uint16_t thumbPalette[16];
  uint32_t thumbTiles[8 * 16 * 14];  // 16x14 tiles
} __attribute__((packed)) SegaVideoHeader;

// After the header is a sequence of chunks.

// Each chunk is:
//  SegaVideoChunkHeader header
//  uint8_t padding[header->paddingBytes]  // aligns samples to 256 bytes
//  uint8_t samples[chunkSoundLen]
//  SegaVideoFrame frames[chunkFrameCount]

typedef struct SegaVideoChunkHeader {
  uint32_t samples;  // in audio, each of which is one byte
  uint16_t frames;  // in video, each of which is a SegaVideoFrame
  // If zero, keep playing after this.  If non-zero, this is the final chunk.
  uint16_t finalChunk;
  // Padding right after the chunk header to maintain 256-byte alignment for
  // the audio data that follows.  The audio driver requires this alignment.
  uint16_t prePaddingBytes;
  // Padding after the last frame to maintain 256-byte alignemnt for the next
  // chunk.  This is slightly wasteful, but makes chunk sizes predictable,
  // removing chunk parsing from the microcontroller.
  uint16_t postPaddingBytes;
} __attribute__((packed)) SegaVideoChunkHeader;

typedef struct SegaVideoFrame {
  // Each palette entry is a single color in ABGR format, 4 bits per channel,
  // in big-endian order.  Entry 0 is considered fully transparent by the VDP,
  // and all other entries are considered fully opaque.  The alpha bits are
  // always ignored.
  uint16_t palette[16];
  // Each tile is a packed array of 64 (8x8) 4-bit palette indexes (16 words).
  // Uses trivial_tilemap_0 or trivial_tilemap_1.
  uint32_t tiles[8 * 32 * 28];  // 32 bytes per tile (8*uint32_t), 32x28 tiles
} __attribute__((packed)) SegaVideoFrame;

// Frames are displayed by alternating between two trivial tilemaps that have
// no deduplication, no priority, and no flipping.  Each tilemap entry is a
// uint16_t value as created by the TILE_ATTR_FULL() macro.  These are ordered
// for each tile, left-to-right, top-to-bottom.  They are precomputed and in
// resource file trivial_tilemap.res.

// Streaming hardware will write chunks to alternating 1MB regions of SRAM.

#endif // _SEGAVIDEO_FORMAT_H
