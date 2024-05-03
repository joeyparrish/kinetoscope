# Kinetoscope Feather Firmware

The hardware is composed of several stacking boards, each of which hosts a set
of subcomponents.  The subcomponents are each in a subsheet, exposing
hierarchical pins and buses to the parent sheet.

One of these boards includes the ESP32 Feather V2 from Adafruit, which has WiFi
and runs its own firmware to take commands from the Sega ROM.

This is the firmware source code, built on the Arduino core for the feather.
The entry point and main loop are in `firmware.ino`.


## Compiler setup

1. Set up the Arduino IDE and ESP32 board as detailed in
   https://learn.adafruit.com/adafruit-esp32-feather-v2/arduino-ide-setup
2. Use the Arduino Library Manager to install the Adafruit MCP23017 library and
   the Adafruit BusIO library
3. To build from the command line, install `arduino-cli` as detailed in
   https://arduino.github.io/arduino-cli/0.35/installation/
4. Run `make`, which will use `arduino-cli` to do everything


## Compile firmware

```sh
make build
```


## Upload firmware

With the feather removed from the cartridge and connected via USB:

```sh
make upload
```
