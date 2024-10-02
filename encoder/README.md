# Sega Video Encoder

Encode videos into a format appropriate for streaming to a Sega Genesis.


## Prerequisites

The encoder requires:
 - Python 3
 - a copy of ffmpeg with PNG and PPM output support

On Ubuntu, you can install these with:

```sh
sudo apt install python3 ffmpeg
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

For a complete example of embedded video in a ROM, see
[`../software/embed-video-in-rom/`](../software/embed-video-in-rom/).

To prepare video for a streaming server ([`server/`](../server/) folder), omit
the `--generate-resource-file` flag, as well as the start and end (`-s` and
`-e`) flags, and add the `--compressed` flag.


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
    [`hardware/`](../hardware/) and the emulation of it in
    [`emulator-patches/`](../emulator-patches/) will not work.

  * `--scene-detection-threshold`: The sensitivity of the scene-change
    detection, which is used to optimize color quantization by choosing a
    stable palette per scene.  The value is a ratio of changed pixels that
    constitutes a new scene, from 0-1.  A threshold of 0 disables scene
    detection and creates an independent palette for each frame.

  * `--dithering`: The dithering algorithm, which controls how the limited
    palette of 15 colors per frame are used to represent the full color
    original image.  The default of "bayer" produces good results for most
    content, but you may prefer "none" in some cases.  For a full list of
    options, see https://ffmpeg.org/ffmpeg-filters.html#paletteuse
