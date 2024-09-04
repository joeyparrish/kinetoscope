# Kinetoscope Hardware Design

The hardware is composed of several stacking boards, each of which hosts a set
of subcomponents.  The subcomponents are each in a subsheet, exposing
hierarchical pins and buses to the parent sheet.

One of these boards includes a microcontroller with WiFi and an ethernet
module, and runs its own firmware to take commands from the Sega ROM.  See
firmware in the `../firmware/` folder.


## Prerequisites

The hardware design was done in KiCad 8.  To install KiCad 8 on Ubuntu, run:

```sh
sudo add-apt-repository --yes ppa:kicad/kicad-8.0-releases
sudo apt update
sudo apt install --install-recommends kicad
```

To install the custom footprints and symbols used in this design, on Ubuntu
run:

```sh
mkdir -p ~/.local/share/kicad/8.0
cp -a hardware/libraries/* ~/.local/share/kicad/8.0/
```

If you have pre-existing KiCad configs, add these new symbol and footprint
libraries to your config via "Preferences" > "Manage Symbol Libraries" and
"Preferences" > "Manage Footprint Libraries".

If you don't have pre-existing KiCad configs, you can copy the tables used in
the automated workflows:

```sh
mkdir -p ~/.config/kicad/8.0
cp .github/workflows/*-lib-table ~/.config/kicad/8.0/
```

To see hardware schematics without KiCad, open the PDF files in the
[`hardware/`](hardware/) folder.

To order boards, see hardware zip files attached to
[releases](https://github.com/joeyparrish/kinetoscope/releases).  These can be
uploaded to JLCPCB.

TODO: Document ordering process


## Boards

The stacking boards are:
 - `cart`: Cartridge Board
   - This is the board that actually plugs into the Sega Genesis.
   - It has 2MB of flash, which acts as the "ROM" chip for addresses 0-2MB, and
     contains the code that runs on the Sega.
   - The flash chip is compatible with the [Krikzz FlashKit Programmer MD][].
   - There is also a pin header on top that the other boards stack onto.
   - The pin header could also be used as a kind of breakout board for the
     development of other Sega cartridge projects.
   - Right-angle pin headers on the sides of the board are optional, and used
     only for debugging.
   - When ordering this through JLCPCB:
     - Be sure to set the following options:
       - Surface Finish: ENIG
       - Gold Fingers: Yes
       - 30°finger chamfered: Yes
     - The following parts will not be properly placed by JLCPCB's systems
       automatically, and must be adjusted manually during the ordering process:
       - J1 (pin header for attaching other boards, wrong location and rotation)
       - U2 (voltage regulator, offset a little too far to the right)
 - `sram-bank`: SRAM Bank Board
   - There are actually two of these in the stack, one for each 1MB SRAM bank.
   - These host the SRAM itself, the buffers that allow it to be alternately
     controlled by the Sega or the microcontroller, and the level shifters that
     allow it to interface to the 5V system of the Sega. This is the most
     complex.  A solder jumper on the board selects whether it responds as bank
     1 or bank 2.
 - `microcontroller`: Microcontroller Board
   - This hosts the microcontroller, voltage regulator, registers, sync token,
     and ethernet module.
   - The registers and sync token allow the Sega to send commands to the
     microcontroller and wait for responses.
   - The microcontroller is a Raspberry Pi Pico W, which is receives commands
     through the registers and sync token, and is responsible for WiFi/Ethernet
     and streaming video to SRAM.


## Sheets

The subsheets are:
 - `address-counter-internal`: SRAM address counter
   - The microcontroller doesn't have enough pins to output the SRAM address it
     wants to write to, so instead, this counter is used to generate sequential
     addresses.
 - `buffer-16-internal`: 16-bit tri-state, level-shifting buffer
   - This buffer allows us to manage control of the 16-bit data bus on the
     SRAM, switching between Sega and microcontroller control when needed.
 - `buffer-22-internal`: 22-bit tri-state, level-shifting buffer
   - This buffer allows us to manage control of the 19-bit address bus and
     control signals on the SRAM, switching between Sega and microcontroller
     control when needed.
 - `data-register-internal`: 16-bit serial-to-parallel shift register
   - The microcontroller doesn't have enough pins to output the SRAM words it
     wants to write, so instead, this register is used to convert a serial
     output from the microcontroller into the parallel input needed by the
     SRAM.
 - `ethernet-internal`: WizNet-5500-based Ethernet module
   - All the circuitry necessary to use wired Ethernet through the WizNet 5500,
     which talks to the microcontroller through an SPI interface.
 - `flash-internal`: Flash connected to buses
   - Flash memory connected to buses to simplify top-level schematics.
 - `kinetoscope-header-internal`: Kinetoscope stacking pin headers
   - The stacking pin headers used for inter-board signals.
 - `register-file-internal`: 4x 8-bit Register File
   - Special registers the Sega writes to and the microcontroller reads from to
     receive commands.
 - `sram-internal`: SRAM connected to buses
   - SRAM connected to buses to simplify top-level schematics.
 - `sync-token-internal`: Shared Sync Token
   - A single bit that can be set by the Sega, cleared by the microcontroller,
     and read by both. The sega sets the bit to tell the microcontroller that a
     command has been written to the registers. The microcontroller clears it
     when the command has been completed.


## Programming

To program the Flash chip in-place:
 1. Insert the cartridge board into the [Krikzz FlashKit Programmer MD][]
 2. Write to it as you would a normal flash cart, using the
    [original FlashKit-MD software][] (GUI) or [FlashKit MD Python Client][]
    (command line)

[Krikzz FlashKit Programmer MD]: https://krikzz.com/our-products/accessories/flashkitmd.html
[original FlashKit-MD software]: https://krikzz.com/pub/support/flashkit-md/
[FlashKit MD Python Client]: https://github.com/joeyparrish/flashkit-md-py
