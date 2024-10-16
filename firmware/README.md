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
2. Install the earlephilhower rp2040 core:
   ```sh
   arduino-cli config add board_manager.additional_urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
   arduino-cli core update-index
   arduino-cli core install rp2040:rp2040
   ```
3. Install the Ethernet library.  Until a new release is out that solves
   https://github.com/arduino-libraries/Ethernet/issues/267,  we install a
   forked version with the fix:
   ```sh
   arduino-cli config set library.enable_unsafe_install true
   arduino-cli lib install --git-url https://github.com/joeyparrish/Ethernet
   ```
4. Set WiFi credentials (see WiFi configuration section below)
5. Run `make`, which will show you a menu of actions.


## WiFi configuration

Create a file called `arduino_secrets.h` with two macros:

```c++
#define SECRET_WIFI_SSID "Put your WiFi SSID here"
#define SECRET_WIFI_PASS "Put your WiFi password here, or blank if none needed"
```

If you don't, the firmware will only be able to use a wired connection.


## Compile firmware

```sh
make build
```


## Upload firmware

With the microcontroller board removed from the cartridge and connected via USB:

```sh
make upload
```
