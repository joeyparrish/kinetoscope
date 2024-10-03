# Kinetoscope: A Sega Genesis Video Player
#
# Copyright (c) 2024 Joey Parrish
#
# See MIT License in LICENSE.txt

# The Kinetoscope RLE format is a byte-based sequence of commands.
#
# The first byte of a command is a control byte.
#
# The top bit (mask 0x80) is the type, and the bottom 7 bits (mask 0x7f)
# are a size field.  The meaning of size depends on the type.
#
# type (mask 0x80):
#   0x00: literal bytes follow, |size| of them, copy directly to output
#   0x80: a single byte follows, repeat |size| times in the output


import os


# How many repeated bytes we need to make compression worth it.  Anything more
# than 2 is technically "worth it" for those bytes themselves, but more
# frequent small repeats means more frequent non-repeating literal sequences,
# too, which increases the overhead for those.  So this is a balancing act.
MIN_REPEAT_FOR_COMPRESSION = 8

# This is the largest number that fits in the size field.  We can't repeat more
# times than this per command, nor output more literal bytes in a row than
# this.
MAX_SIZE_FIELD = 127

# Constants for the type bit.
TYPE_LITERAL = 0x00
TYPE_REPEAT = 0x80


def _count_repeats(block, offset):
  original_offset = offset
  while offset < len(block) and block[offset] == block[original_offset]:
    offset += 1
  # Offset is now the number of repeats.
  return offset - original_offset


def rle_compress(block):
  # The compressed output.
  output = b''

  # A buffer of literals to be flushed later.
  literals = b''

  def flush_buffered_literals():
    # Take these from the outer scope
    nonlocal literals, output

    offset = 0
    while offset < len(literals):
      # Don't output more at once than fits in this size field
      literal_block_size = min(len(literals) - offset, MAX_SIZE_FIELD)
      literal_block = literals[offset:offset+literal_block_size]
      offset += literal_block_size

      control_byte = TYPE_LITERAL | literal_block_size
      output += control_byte.to_bytes(1, 'big')
      output += literal_block

    literals = b''

  def compress_repeats(data, count):
    # Take these from the outer scope
    nonlocal output

    while count:
      # Don't output more at once than fits in this size field
      repeat_count = min(count, MAX_SIZE_FIELD)
      count -= repeat_count

      control_byte = TYPE_REPEAT | repeat_count
      output += control_byte.to_bytes(1, 'big')
      output += data

  i = 0
  while i < len(block):
    count = _count_repeats(block, i)
    this_byte = block[i:i+1]  # Still bytes type, not int as block[i] would be
    i += count

    if count < MIN_REPEAT_FOR_COMPRESSION:
      # Buffer literals for later
      literals += this_byte
    else:
      # Flush buffered literals first
      flush_buffered_literals()
      # Compress repeated sequence
      compress_repeats(this_byte, count)

  # Flush any remaining buffered literals
  flush_buffered_literals()
  return output
