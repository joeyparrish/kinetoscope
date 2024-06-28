<h1>
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="logo-dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="logo.svg">
    <img alt="Kinetoscope Logo" src="logo.svg">
  </picture>
</h1>

A Sega Genesis / Mega Drive Video Player


## Overview

Kinetoscope can either play video embedded in a standard ROM, or it can stream
video over WiFi using special hardware in a custom cartridge.  It uses a custom
video format detailed in
[`player/inc/segavideo_format.h`](player/inc/segavideo_format.h) and encoded by
[`encoder/encode_sega_video.py`](encoder/encode_sega_video.py).

Kinetoscope takes its name from the [first moving picture
device](https://www.britannica.com/technology/Kinetoscope), invented in 1891 by
Thomas Edison and William Dickson.  While the 1891 original Kinetoscope was
able to display 46 frames per second, this project for a 1988 game console can
only push 10.

The default format is displayed at ~320x240p analog SD resolution at 10fps,
with 8-bit PCM audio at 13kHz.  The encoded pixels are not square.  The encoded
frames use a 256x224 resolution due to hardware limitations, and are displayed
at a different ratio by the Sega's Video Display Processor (VDP).

If video is embedded in the ROM, you can only fit about 13.6 seconds in a 4MB
cartridge/ROM.

Schematics and board layouts for special streaming hardware can be found in the
[`hardware/`](hardware/) folder.


## Prerequisites

The encoder requires:
 - Python 3
 - a copy of ffmpeg with PNG and PPM output support

The [SGDK][] compiler requires:
 - Docker

On Ubuntu, you can install these with:

```sh
sudo apt install python3 ffmpeg docker.io
```

If the video is embedded in the ROM with the player, nothing else is required.
A standard flash cart or ROM will work fine.

To stream video over WiFi, special hardware is required.


## Project Folders

 - [`player/`](player/): The player library, written for [SGDK][], which you
   can import into your own projects if you wish.
 - [`encoder/`](encoder/): The video encoder, which generates videos in an
   appropriate format for embedding or streaming.
 - [`embed-video-in-rom/`](embed-video-in-rom/): A sample project using
   [SGDK][] and the player library that allows you to easily embed a video
   stream into a ROM and play it back when the program starts.  This works fine
   in a standard ROM+emulator or in a flash cart.
 - [`hardware/`](hardware/): The schematics and board layouts for a custom
   cartridge with special streaming hardware.
 - [`firmware/`](firmware/): The firmware that runs on the microcontroller of
   the custom cartridge hardware.
 - [`stream-with-special-hardware/`](stream-with-special-hardware/): A project
   using [SGDK][] that builds a small ROM that can communicate with the
   streaming hardware from [`hardware/`](hardware/), stream video over WiFi,
   and play back a virtually unlimited stream.  This will not work in a
   standard emulator without special emulation for this hardware.  See also
   [`emulator-patches/`](emulator-patches/).
 - [`emulator-patches/`](emulator-patches/): Patches for OSS emulators to
   emulate the streaming hardware.
 - [`server/`](server/): Details on running a server for Sega video streams.
 - [`canned-videos/`](canned-videos/): Install canned, pre-encoded videos for
   use in an emulator or video server.


## Links

 - [SGDK][]: A free and open development kit for the Sega Genesis / Mega Drive
 - The logo font is [Kode Mono](https://kodemono.com/)
 - Presentation slides coming soon
 - [Sega Genesis Slide Generator and Viewer](https://github.com/joeyparrish/sega-slides/)
 - [Krikzz](https://krikzz.com/our-products/cartridges/): Incredible hardware
   for retro game console hacking and development, including flash carts.


[SGDK]: https://github.com/Stephane-D/SGDK
