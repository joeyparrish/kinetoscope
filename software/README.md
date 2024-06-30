# Kinetoscope Software

The software in loaded onto a cartridge to run on a Sega Genesis / Mega Driver,
or can be run on an appropriate emulator.


## Prerequisites

Compiling for Sega using [SGDK][] requires:
 - Docker

On Ubuntu, you can install this with:

```sh
sudo apt install docker.io
```

See also the video encoder in [`../encoder/`](../encoder/).

If the video is embedded in the ROM with the player, nothing else is required.
A standard flash cart or emulator will work fine.

To stream video over WiFi, special hardware is required.  See
[`../hardware/`](../hardware/), or
[`../emulator-patches/`](../emulator-patches/) for emulation of that hardware.


## Project Folders

 - [`player/`](player/): The player library, written for [SGDK][], which you
   can import into your own projects if you wish.
 - [`embed-video-in-rom/`](embed-video-in-rom/): A sample project using
   [SGDK][] and the player library that allows you to easily embed a video
   stream into a ROM and play it back when the program starts.  This works fine
   in a standard ROM+emulator or in a flash cart.
 - [`stream-with-special-hardware/`](stream-with-special-hardware/): A project
   using [SGDK][] that builds a small ROM that can communicate with the
   streaming hardware from [`../hardware/`](../hardware/), stream video over
   WiFi, and play back a virtually unlimited stream.  This will not work in a
   standard emulator without special emulation for this hardware.  See also
   [`../emulator-patches/`](../emulator-patches/).


[SGDK]: https://github.com/Stephane-D/SGDK
