#!/usr/bin/env python3

# Kinetoscope: A Sega Genesis Video Player
#
# Copyright (c) 2024 Joey Parrish
#
# See MIT License in LICENSE.txt

"""Create the video catalog needed to for streaming to a Sega Genesis.

Usage: python3 generate_catalog.py *.segavideo

Output will be in catalog.bin, which is the required filename for your server.

See also ../encoder/encode_sega_video.py
"""

import os
import sys


def get_video_header(path):
  with open(path, 'rb') as f:
    header = f.read(8192)
  assert len(header) == 8192

  # The header parts before and after the relative_url field.
  first_part = header[0:(30+128)]
  last_part = header[(30+128+128):]

  # Compute the relative URL.
  relative_url = os.path.relpath(path)
  if relative_url[0:2] == '..':
    raise RuntimeError('All paths must be inside this folder!')

  # Encode the relative URL.
  relative_url = relative_url.encode('utf-8')
  if len(relative_url) > 127:
    raise RuntimeError('Relative paths cannot be more than 127 bytes.')

  # Truncate/pad to 128 bytes including terminator.
  relative_url = (relative_url + bytes(128))[0:127] + b'\0'
  assert len(relative_url) == 128

  return first_part + relative_url + last_part


def main(paths):
  if len(paths) > 127:
    raise RuntimeError('No more than 127 videos can fit in a catalog.')

  with open('catalog.bin', 'wb') as f:
    for path in paths:
      print('Processing', path)
      f.write(get_video_header(path))

    # End the catalog with a blank header.
    f.write(bytes(8192))

  print('Generated catalog.bin')


if __name__ == '__main__':
  main(sys.argv[1:])
