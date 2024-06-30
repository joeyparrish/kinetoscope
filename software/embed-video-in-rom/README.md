# Embedded video in the ROM


## Prerequisites

The encoder requires:
 - Python 3
 - a copy of ffmpeg with PNG and PPM output support

The SGDK compiler requires:
 - Docker

On Ubuntu, you can install these with:

```sh
sudo apt install python3 ffmpeg docker.io
```


## Instructions

To make a 10-second clip, from time 0 to time 10, run:

```sh
../../encoder/encode_sega_video.py \
  -i /path/to/never-gonna-give-you-up.mp4 \
  -s 0 -e 10 \
  -o res/video_data.segavideo \
  --generate-resource-file
```

Adjust the input file (`-i`), start time (`-s`), and end time (`-e`) however
you like, but you can only fit about 13.6 seconds on a 4MB ROM with enough room
left for the player.

After generating the video, build the ROM with the build script:

```sh
./build.sh
```

The output will be in `out/rom.bin`.
