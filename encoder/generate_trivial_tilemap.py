#!/usr/bin/env python3

# Kinetoscope: A Sega Genesis Video Player
#
# Copyright (c) 2024 Joey Parrish
#
# See MIT License in LICENSE.txt

"""Generate the trivial tilemap resources that are used by the video player.

These are already committed to the repo under the player folder, so this does
not need to be rerun per app."""

def write_trivial_tilemap(path, pal_num):
  with open(path, 'wb') as f:
    # A full screen of tiles at the player's resolution is 32x28 tiles.
    # The tiles are placed left-to-right, top-to-bottom in a trivial order.
    for i in range(32*28):
      # Stripped down version of TILE_ATTR_FULL() macro without priority or
      # flipping.  Assuming no deduplication of tiles.
      map_value = (pal_num << 13) | i
      f.write(map_value.to_bytes(2, 'big'))

write_trivial_tilemap('trivial_tilemap_0.bin', 0)
write_trivial_tilemap('trivial_tilemap_1.bin', 1)

with open('trivial_tilemap.res', 'w') as f:
  f.write('BIN trivial_tilemap_0 trivial_tilemap_0.bin 256\n')
  f.write('BIN trivial_tilemap_1 trivial_tilemap_1.bin 256\n')
