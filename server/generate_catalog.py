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
import urllib.parse


def parse_video(path):
  print('Parsing', path)

  with open(path, 'rb') as f:
    header = f.read(8192)
  assert len(header) == 8192

  # The header parts before and after the relative_url field.
  first_part = header[0:38]
  title_bytes = header[38:(38+128)]
  # relative_url goes here
  last_part = header[(38+128+128):]

  # The title field as a string.
  title = title_bytes.rstrip(b'\x00').decode('utf-8')

  # Compute the relative URL.
  relative_url = os.path.relpath(path)
  if relative_url[0:2] == '..':
    raise RuntimeError('All paths must be inside this folder!')

  # Encode the relative URL.
  relative_url = urllib.parse.quote(relative_url).encode('utf-8')
  if len(relative_url) > 127:
    raise RuntimeError('Relative paths cannot be more than 127 bytes after URL encoding.')

  # Truncate/pad to 128 bytes including terminator.
  relative_url = (relative_url + bytes(128))[0:127] + b'\0'
  assert len(relative_url) == 128

  return {
    'title': title,
    'catalog_header': first_part + title_bytes + relative_url + last_part,
  }


def main(paths):
  if len(paths) > 127:
    raise RuntimeError('No more than 127 videos can fit in a catalog.')

  metadata = [ parse_video(path) for path in paths ]
  metadata.sort(key=lambda item: item['title'])

  with open('catalog.bin', 'wb') as f:
    for item in metadata:
      f.write(item['catalog_header'])

    # End the catalog with a blank header.
    f.write(bytes(8192))

  print('Generated catalog.bin')


if __name__ == '__main__':
  main(sys.argv[1:])
