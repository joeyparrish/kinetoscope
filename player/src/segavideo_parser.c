// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Sega video parser routines, shared between projects.
//
// This can run on the Sega, inside an emulator, or in the firmware of the
// streaming hardware.

#if defined(SGDK_GCC)
// Always big-endian, running on the Sega.
# define ntohs(x) (x)
# define ntohl(x) (x)
// Define kprintf.
# include <genesis.h>
// No definition for this in SGDK:
  typedef uint32_t uintptr_t;
#elif defined(_WIN32)
// Windows header for ntohs and ntohl.
# include <winsock2.h>
// Print directly.
# include <stdio.h>
# define kprintf printf
#else
// Linux headers for ntohs and ntohl.
# include <arpa/inet.h>
# include <netinet/in.h>
// Print directly.
# include <stdio.h>
# define kprintf printf
#endif

#include "segavideo_parser.h"
#include "segavideo_format.h"

bool segavideo_validateHeader(const uint8_t* videoData) {
  const SegaVideoHeader* header = (const SegaVideoHeader*)videoData;

  // No memcmp in SGDK...
  for (uint16_t i = 0; i < sizeof(header->magic); ++i) {
    if (header->magic[i] != SEGAVIDEO_HEADER_MAGIC[i]) {
      kprintf("Header magic does not match!  Wrong format?\n");
      return false;
    }
  }

  if (ntohs(header->format) != SEGAVIDEO_HEADER_FORMAT) {
    kprintf("Header format does not match!  New revision?\n");
    return false;
  }

  return true;
}

void segavideo_parseChunk(const uint8_t* chunkStart,
                          SegaVideoChunkInfo* chunkInfo) {
  const SegaVideoChunkHeader* chunkHeader =
      (const SegaVideoChunkHeader*)chunkStart;
  chunkInfo->start = chunkStart;
  chunkInfo->audioStart =
      chunkStart + sizeof(SegaVideoChunkHeader) +
      ntohs(chunkHeader->paddingBytes);
  chunkInfo->audioSamples = ntohl(chunkHeader->samples);
  chunkInfo->frameStart = chunkInfo->audioStart + chunkInfo->audioSamples;
  chunkInfo->numFrames = ntohs(chunkHeader->frames);
  chunkInfo->end =
      chunkInfo->frameStart + sizeof(SegaVideoFrame) * chunkInfo->numFrames;
}
