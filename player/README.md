# Kinetoscope Player Library

This is the player library for Kinetoscope, built on [SGDK][].

Its functionality breaks into two broad categories:

1. Parse and play video, which may or may not be embedded in the ROM
2. Communicate with special hardware to stream video over WiFi


## Embedded playback

To play video embedded in the ROM, you will need to:

1. Include the player library sources, headers, and static resources into your
   SGDK project.
2. Embed the video binary embedded as a resource, aligned to 256 bytes.
3. Call `segavideo_init()` early in `main()`.  This will set the video mode to
   256x224 to match the encoding.
4. Call `segavideo_play(video_data, /* loop= */ true)` to start looping the
   embedded video.
5. In your while loop, call `segavideo_processFrames()` followed by
   `SYS_doVBlankProcess()`.

For a sample project, see the `embed-video-in-rom/` folder.


## Streaming video over WiFi

To stream video over WiFi, you will need to:

1. Include the player library sources, headers, and static resources into your
   SGDK project.
2. Have the right hardware (`hardware/` folder) or emulator patches
   (`emulator-patches/` folder).
3. Call `segavideo_init()` early in `main()`.  This will set the video mode to
   256x224 to match the encoding.
4. Call `segavideo_checkHardware()` to confirm that the streaming hardware is
   available.  Abort if false.
5. Call `segavideo_getMenu()` to download a menu of videos from the server.
   Abort if false.
6. Call `segavideo_drawMenu()` to display the menu.
7. While `segavideo_isMenuShowing()`, call `segavideo_drawMenu()` followed by
   `SYS_doVBlankProcess()`.
8. On user input, call `segavideo_menuPreviousItem()` and
   `segavideo_menuNextItem()` to move through the menu, and
   `segavideo_stream(/* loop= */ false)` to choose the current item.
9. While `segavideo_isPlaying()`, call `segavideo_processFrames()` followed by
   `SYS_doVBlankProcess()`.

For a sample project, see the `stream-with-special-hardware/` folder.


## Technical details

### Video format

The video format is detailed in `inc/segavideo_format.h`.

It begins with an overall header with information about the total number of
frames, audio samples, etc.  Then it proceeds as a series of chunks, each of
which has its own header.

The chunk header tells you how many frames, audio samples, and padding bytes
will follow.  Next you find audio samples, which are aligned to a 256-byte
boundary by the padding bytes.  The audio driver requires this alignment.  Next
you find the frames.

Audio samples are 8-bit signed PCM at 13kHz (13312 Hz).  This is the fixed
format and playback rate of SGDK's XGM2 audio driver.  It is the only driver so
far that plays smoothly and without distortion over a long time while we are
simultaneously pushing video frames to the VDP (Video Display Processor).

The frame format is a 16-color palette, followed by 32x28 tiles.

The palette format is the native format of the Sega's VDP so it can be
transfered directly to video memory: 16x 16-bit words, each of which represents
one color.  Colors are in ABGR format, 4 bits per channel, big-endian.  The
alpha bits are always ignored by the VDP.  Entry 0 is always considered fully
transparent, and all other entries are always considered fully opaque.

The tile format is also native to the Sega's VDP.  The VDP represents game
backgrounds as a plane of tiles.  Each tile is 8x8 pixels, and it supports both
40-tile-wide (320-pixel-wide) and 32-tile-wide (256-pixel-wide) modes.  We use
the less-taxing 32-tile-wide mode for video.  The overall resolution is 32x28
tiles, or 256x224 pixels.

At the above frame size, we can push at most 10 frames per second before we
start dropping frames, and frame-dropping will break chunked streaming.  (See
below.)  So the video encoder defaults to 10 fps.

The colors in each tile refer to one of the 16-color palettes.  Each pixel is
encoded as 4 bits, the index of an entry in the associated palette.  There are
4 pixels per 16-bit word, so each 8x8 tile takes 16 words or 32 bytes.  Pixel
order is left-to-right, top-to-bottom.  The full set of 32x28 tiles is also
ordered left-to-right, top-to-bottom.

Each full-screen frame at 32x28 tiles x 32 bytes per tile takes up 28 kB + 16
bytes for the palette.

At 10 fps and 13 kHz, each second of audio+video takes up ~293 kB.  The overall
bandwidth requirement for streaming is therefore ~2.3 Mbit/s.

### Chunked streaming

The video is broken into chunks that are at most 1MB in size, to match the
streaming hardware's SRAM.

The video streaming hardware has two regions of 1MB of SRAM.  If a chunk the
same size as the previous one would overflow the current region, the next chunk
will be written to the other region instead of the current region.  The
hardware will adjust the padding to ensure a 256-byte alignment of audio
samples, as required by the audio driver.

When the player finishes reading from one region and moves to the next, it
signals the streaming hardware, which can then begin filling the opposite
region.  In this way, the player is always reading from one region while the
streaming hardware is writing to the other, and an unlimited stream can be
played.

The end of the video is signaled by a chunk header with 0 frames and 0 audio
samples.
