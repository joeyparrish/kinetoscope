# Streaming Video over WiFi


## Prerequisites

You must have one of:
 - Special video streaming hardware in a custom cartridge (see
   [`../../hardware/`](../../hardware/) folder)
 - An emulator that emulates this special hardware

To build the ROM, we use the SGDK compiler via Docker.

On Ubuntu, you can install this with:

```sh
sudo apt install docker.io
```


### Real Hardware

Schematics and board layouts for the real hardware can be found in the
[`../../hardware/`](../../hardware/) folder.

The real hardware will need to have its firmware updated with WiFi connection
info such as SSID and password, as well as a server URL to pull video from (see
[`../../server/`](../../server/) folder).


### Emulator

Patches for OSS Sega emulators can be found in the
[`../../emulator-patches/`](../../emulator-patches/) folder, along with
instructions on how to store pre-encoded videos to simulate a video server.


## Compilation

Build the ROM with the build script:

```sh
./build.sh
```

The output will be in `out/rom.bin`.


## Running

Run the ROM in a patched emulator (see
[`../../emulator-patches/`](../../emulator-patches/) for details), or flash it
to a custom cartridge (see [`../../hardware/`](../../hardware/) for details).
