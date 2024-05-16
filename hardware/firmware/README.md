# Kinetoscope Microcontroller Firmware

The hardware is composed of several stacking boards, each of which hosts a set
of subcomponents.  The subcomponents are each in a subsheet, exposing
hierarchical pins and buses to the parent sheet.

One of these boards includes a microcontroller, which has an interface to the
internet and runs its own firmware to take commands from the Sega ROM.

This is the firmware source code, built on the Arduino core for the
microcontroller.
The entry point and main loop are in `firmware.ino`.


## Compiler setup

1. To build from the command line, install `arduino-cli` as detailed in
   https://arduino.github.io/arduino-cli/0.35/installation/
2. FIXME: Install arduino-cli
3. FIXME: Install the board core
4. FIXME: Install libraries
5. Run `make`, which will use `arduino-cli` to do everything.


## WiFi configuration

Create a file called `arduino_secrets.h` with two macros:

```c++
#define SECRET_WIFI_SSID "Put your WiFi SSID here"
#define SECRET_WIFI_PASS "Put your WiFi password here"
```

The firmware cannot be compiled without this.


## Compile firmware

```sh
make build
```


## Upload firmware

With the microcontroller board removed from the cartridge and connected via USB:

```sh
make upload
```
