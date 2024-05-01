# Kinetoscope Hardware Design

The hardware is composed of several stacking boards, each of which hosts a set
of subcomponents.  The subcomponents are each in a subsheet, exposing
hierarchical pins and buses to the parent sheet.

## Boards

The stacking boards are:
 - `sram-bank`: SRAM Bank Board
   - There are actually two of these in the stack, one for each 1MB SRAM bank.
   - These host the SRAM itself, the buffers that allow it to be alternately
     controlled by the Sega or the ESP32 feather, and the level shifters that
     allow it to interface to the 5V system of the Sega. This is the most
     complex.  A solder jumper on the board selects whether it responds as bank
     1 or bank 2.
 - `registers`: Register & Flash Board
   - This hosts the registers, sync token, and flash "ROM" chips.
   - The registers and sync token allow the Sega to send commands to the
     feather and wait for responses.  The flash chips act as the "ROM" chip,
     and contain the code that runs on the Sega.
 - `feather`: Feather and Decoder Board
   - This hosts the Adafruit ESP32 feather, which is responsible for WiFi and
     writing to SRAM, and the signal decoder logic, which decodes Sega address
     lines and other signals to generate the control signals for the various
     subcomponents to avoid bus conflicts.
 - `cart`: Cartridge Edge Connector Board
   - This is the board that actually plugs into the Sega Genesis.  It has a pin
     header on top that the other boards stack onto.  This interface could also
     be used as a kind of breakout board for the development of other Sega
     cartridge projects.

## Sheets

The subsheets are:
 - `address-counter-internal`: SRAM Address Counter
   - The feather doesn't have enough pins to output the SRAM address it wants
     to write to, so instead, this counter is used to generate sequential
     addresses.
 - `buffer-16-internal`: 16-bit tri-state, level-shifting buffer
   - This buffer allows us to manage control of the 16-bit data bus on the
     SRAM, switching between Sega and feather control when needed.
 - `buffer-22-internal`: 22-bit tri-state, level-shifting buffer
   - This buffer allows us to manage control of the 19-bit address bus and
     control signals on the SRAM, switching between Sega and feather control
     when needed.
 - `data-register-internal`: 16-bit serial-to-parallel shift register
   - The feather doesn't have enough pins to output the SRAM words it wants to
     write, so instead, this register is used to convert a serial output from
     the feather into the parallel input needed by the SRAM.
 - `decoder-internal`: Control Signal Decoder
   - Decodes the addresses and control signals from the Sega into the control
     signals for all the other various components.
 - `flash-internal`: Flash connected to buses
   - Flash memory connected to buses to simplify top-level schematics.
 - `kinetoscope-header-internal`: Kinetoscope stacking pin headers
   - The stacking pin headers used for inter-board signals.
 - `port-expander-internal`: Port expander connected to buses
   - Port expander IC connected to buses to simplify top-level schematics.
 - `register-file-internal`: 4x 8-bit Register File
   - Special registers the Sega writes to and the feather reads from to receive
     commands.
 - `sram-internal`: SRAM connected to buses
   - SRAM connected to buses to simplify top-level schematics.
 - `sync-token-internal`: Shared Sync Token
   - A single bit that can be set by the Sega, cleared by the feather, and read
     by both. The sega sets the bit to tell the feather that a command has been
     written to the registers. The feather clears it when the command has been
     completed.

## Programming

To program the Flash chip in-place:
 1. Stack the Register & Flash board on top of the Cart board, omitting all
    other boards
 2. Place a jumper from the `~{SEGA_WE_LB}` pin (#46, fourth from the right on
    the top row of the lower connector) to the `~{FLASH_WE}` pin (#55, second
    from the left on the bottom row of the upper connector)
 3. Insert the cart into the
    [Krikzz FlashKit Programmer MD](https://krikzz.com/our-products/accessories/flashkitmd.html)
