// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Shared RLE code.

// If we're processing a repeat, but don't have the actual byte to be repeated,
// this stores the number of repeats.  If we're not, it's 0.
static int _rle_pending_repeats = 0;
// If we're in the middle of a sequence of literal bytes spread across multiple
// input buffers, this is the counter for how many are left.  If we're not,
// it's 0.
static int _rle_pending_literals = 0;

#if !defined(MIN)
# define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

static int _rle_output_literals(const uint8_t* data, int bytes, int needed) {
  // We are still copying literal bytes to the output stream.
  // How many should we copy?  The number we need, or the number we have,
  // whichever is smaller.
  int available = MIN(needed, bytes);
  SRAM_WRITE(data, available);
  return available;
}

static void _rle_output_repeats(uint8_t data_byte, int repeats) {
  for (int i = 0; i < repeats; ++i) {
    SRAM_WRITE(&data_byte, 1);
  }
}

/**
 * Requires these macros:
 *
 * #define SRAM_WRITE(buffer, size)
 */
static void rle_to_sram(const uint8_t* buffer, int bytes) {
  if (!bytes) {
    // This shouldn't happen, but if it did, our logic would break.
    return;
  }

  if (_rle_pending_repeats) {
    // Now we have the data to process this pending repeat command from another
    // input buffer.
    _rle_output_repeats(*buffer, _rle_pending_repeats);
    // This consumes one byte from buffer.
    buffer++;
    bytes--;
    // The pending repeat is satisfied.
    _rle_pending_repeats = 0;
  } else if (_rle_pending_literals) {
    // We are still processing literals from another input buffer.
    int consumed = _rle_output_literals(buffer, bytes, _rle_pending_literals);
    // This consumed some number of bytes.
    buffer += consumed;
    bytes -= consumed;
    // Our pending literals are also reduced by the same amount.
    _rle_pending_literals -= consumed;
  }

  while (bytes) {
    // Extract the control byte.
    uint8_t control_byte = buffer[0];
    buffer++;
    bytes--;

    // Parse it.
    bool repeat = control_byte & 0x80;
    int size = control_byte & 0x7f;

    if (repeat) {
      if (!bytes) {
        // We don't have the byte to repeat.
        // Save the size for next time.
        _rle_pending_repeats = size;
        return;
      }

      // Repeat the next byte.
      _rle_output_repeats(*buffer, size);

      // Consume that byte.
      buffer++;
      bytes--;
    } else {
      // Output literal bytes, as many as we can.  (This could be zero.)
      int consumed = _rle_output_literals(buffer, bytes, size);

      // Consume that data.
      buffer += consumed;
      bytes -= consumed;

      // Store the number of unfulfilled literal bytes we need from the next
      // buffer.  (This could be zero.)
      _rle_pending_literals = size - consumed;
    }
  }
}

static void rle_reset() {
  _rle_pending_repeats = 0;
  _rle_pending_literals = 0;
}
