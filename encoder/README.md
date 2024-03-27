# Sega Video Encoder

Encode videos into a format appropriate for streaming to a Sega Genesis.


## Prerequisites

The encoder requires:
 - Python 3
 - ImageMagick
 - a copy of FFmpeg with PNG output support

On Ubuntu, you can install these with:

```sh
sudo apt install python3 imagemagick ffmpeg
```


## Instructions

To make a 10-second clip, from time 0 to time 10, run:

```sh
./encode_sega_video.py \
  -i /path/to/never-gonna-give-you-up.mp4 \
  -s 0 -e 10 \
  -o res/video_data.segavideo \
  --generate-resource-file
```

Adjust the input file (`-i`), start time (`-s`), and end time (`-e`) however
you like, but you can only fit about 13.6 seconds on a 4MB ROM with enough room
left for the player.

The video output file (`-o`) contains the actual video stream.  If you
generated an SGDK resource file (`--generate-resource-file`), it will have the
same name as the output, but end in `.res`.  SGDK will generate a header file
next to it that ends in `.h`, which you will reference in your code to get the
address of the video in ROM.

For a complete example of embedded video in a ROM, see the
`embed-video-in-rom/` folder.

To prepare video for a streaming server (`server/` folder) or for use by an
emulator (`emulator-patches/` folder), omit the `--generate-resource-file`
flag, as well as the start and end (`-s` and `-e`) flags.


## Other Settings

You can see a full list of settings with `./encode_sega_video.py --help`.  Here
are a few that require additional notes:

  * `--fps`: The maximum video frame rate we have managed to sustain is 10fps.
    Above that, we drop frames, which risks breaking the careful
    synchronization of chunked streaming between the Sega's M68k CPU and the
    streaming coprocessor.

  * `--sample-rate`: The audio sample rate is technically not fixed in the
    video file format, but the only performant audio driver that we have gotten
    to work so far is SGDK's XGM2 driver, which has a fixed sample rate of
    13312 Hz.  If you use a different rate, the audio will play at the wrong
    speed.  A new Z80 audio driver will be needed in the player to lift this
    restriction.

  * `--chunk-length`: The default chunk length is tuned to make chunks at the
    default frame rate and sample rate fit inside the SRAM buffer of our
    special hardware.  If the chunk size exceeds 1MB, the streaming hardware in
    `hardware/` and the emulation of it in `emulator-patches/` will not work.

  * `--detect-scenes`: An experimental flag to optimize the color palette by
    scene, rather than frame-by-frame.  This consumes a lot of memory and has
    not yet demonstrated a concrete benefit to quality, so it is currently off
    by default.  See also `--scene-detection-threshold` under `--help`.
