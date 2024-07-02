#!/usr/bin/env python3

# Kinetoscope: A Sega Genesis Video Player
#
# Copyright (c) 2024 Joey Parrish
#
# See MIT License in LICENSE.txt

"""Generate the trivial tilemap resources that are used by the video player.

These are already committed to the repo under the player folder, so this does
not need to be rerun per app."""

def write_trivial_tilemap(path, pal_num, width, height):
  with open(path, 'wb') as f:
    # The tiles are placed left-to-right, top-to-bottom in a trivial order.
    for i in range(width * height):
      # Stripped down version of TILE_ATTR_FULL() macro without priority or
      # flipping.  Assuming no deduplication of tiles.
      map_value = (pal_num << 13) | i
      f.write(map_value.to_bytes(2, 'big'))

# A full screen of tiles at the player's resolution is 32x28 tiles.
write_trivial_tilemap('trivial_tilemap_0.bin', 0, 32, 28)
write_trivial_tilemap('trivial_tilemap_1.bin', 1, 32, 28)

# A half-sized tilemap for thumbnails, only in palette 0.
write_trivial_tilemap('trivial_tilemap_half_0.bin', 0, 16, 14)

with open('trivial_tilemap.res', 'w') as f:
  f.write('BIN trivial_tilemap_0 trivial_tilemap_0.bin 256\n')
  f.write('BIN trivial_tilemap_1 trivial_tilemap_1.bin 256\n')
  f.write('BIN trivial_tilemap_half_0 trivial_tilemap_half_0.bin 256\n')
