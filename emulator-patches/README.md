# Emulator Patches

To emulate Kinetoscope hardware, you will need to patch an OSS Sega emulator.
We provide patches for the following emulators:

 - [BlastEm](https://github.com/libretro/blastem):
   1. `git clone https://github.com/libretro/blastem`
   2. `cd blastem`
   3. `git checkout 277e4a62`  # Other revisions may also work
   4. `git clone https://github.com/joeyparrish/kinetoscope`
   5. `patch -p1 -i kinetoscope/emulator-patches/blastem-0.6.2.patch`
   6. `make` or `./build_release`

 - [Genesis Plus GX](https://github.com/libretro/Genesis-Plus-GX):
   1. `git clone https://github.com/libretro/Genesis-Plus-GX`
   2. `cd Genesis-Plus-GX`
   3. `git checkout 7de0f0b6`  # Other revisions may also work
   4. `git clone https://github.com/joeyparrish/kinetoscope core/kinetoscope`
   5. `patch -p1 -i core/kinetoscope/emulator-patches/genesis-plus-gx.patch`
   6. Compile as usual (varies widely by target platform).  To compile with
      Emscripten for the web:

      1. `git clone https://github.com/emscripten-core/emsdk`
      2. `emsdk/emsdk install 3.1.46`
      3. `emsdk/emsdk install 3.1.46`
      4. `source emsdk/emsdk_env.sh`
      5. `emmake make -f Makefile.libretro platform=emscripten`
      6. `git clone https://github.com/libretro/RetroArch`
      7. `cp *_libretro_emscripten.bc RetroArch/libretro_emscripten.bc`
      8. `cd RetroArch`
      9. `emmake make -f Makefile.emscripten LIBRETRO=genesis_plus_gx 'LIBS=-s USE_ZLIB=1 -s FETCH=1' -j all`
      10. Deploy `genesis_plus_gx_libretro.*`


## Pre-built emulators

Binary builds of BlastEm (for Linux, Windows, and macOS) and Genesis Plus GX
(for the web via retroarch, nostalgist.js, or any other libretro-compatible JS
project) are available from the releases page:

https://github.com/joeyparrish/kinetoscope/releases


## Licensing

These patches and Kinetoscope code in general are licensed under the MIT
license found in LICENSE.txt.

BlastEm is licensed under GPLv3.  BlastEm licensing is compatible with our
patches, but the final build of BlastEm using these patches must be distributed
under GPLv3.

Genesis Plus GX is licensed under a variety of open source licenses.  See
https://github.com/libretro/Genesis-Plus-GX/blob/master/LICENSE.txt for full
details.  Kinetoscope's MIT license is compatible with these, and the final
build of Genesis Plus GX is available under that project's original terms.

See also https://libguides.wvu.edu/c.php?g=1260463&p=9239106#s-lg-box-29255221
