# Emulator Patches

To emulate Kinetoscope hardware, you will need to patch an OSS Sega emulator.
We provide patches for the following emulators:

 - [BlastEm](https://github.com/libretro/blastem):
   1. `git clone https://github.com/libretro/blastem`
   2. `cd blastem`
   3. `git checkout 277e4a62`  # Other revisions may also work
   4. `git clone https://github.com/joeyparrish/kinetoscope`
   5. `patch -p1 -i kinetoscope/emulator-patches/blastem-0.6.2.patch`
   6. `make`
