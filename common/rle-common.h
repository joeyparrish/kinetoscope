// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Shared RLE code.

static int _rle_control_byte = -1;
static int _rle_more_literals = 0;

static int _rle_output_literals(const uint8_t* data, int bytes);
static void _rle_process(uint8_t control_byte, const uint8_t* data, int bytes);

#if !defined(MIN)
# define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/**
 * Requires these macros:
 *
 * #define SRAM_WRITE(buffer, size)
 */
static void rle_to_sram(const uint8_t* buffer, int bytes) {
  if (_rle_control_byte != -1) {
    // Now we have the data to process this cached control byte from another
    // callback.
    int control_byte = _rle_control_byte;
    _rle_control_byte = -1;  // clear cache
    _rle_process(control_byte, buffer, bytes);
  } else if (_rle_more_literals) {
    int consumed = _rle_output_literals(buffer, bytes);
    buffer += consumed;
    bytes -= consumed;
  }

  if (bytes) {
    _rle_process(buffer[0], buffer + 1, bytes - 1);
  }
}

static void rle_reset() {
  _rle_control_byte = -1;
  _rle_more_literals = 0;
}

static int _rle_output_literals(const uint8_t* data, int bytes) {
  // We are still copying literal bytes to the output stream.
  int available = MIN(_rle_more_literals, bytes);
  SRAM_WRITE(data, available);
  _rle_more_literals -= available;
  return available;
}

static void _rle_process(uint8_t control_byte, const uint8_t* data, int bytes) {
  while (true) {
    bool repeat = control_byte & 0x80;
    int size = control_byte & 0x7f;

    if (repeat) {
      if (!bytes) {
        // We don't have the byte to repeat.
        // Save the control byte for next time and return.
        _rle_control_byte = control_byte;
        return;
      }

      // Output the next byte |size| times.
      for (int i = 0; i < size; ++i) {
        SRAM_WRITE(data, 1);
      }

      // Consume that byte.
      data++;
      bytes--;
    } else {
      _rle_more_literals = size;
      int consumed = _rle_output_literals(data, bytes);
      data += consumed;
      bytes -= consumed;
    }

    if (!bytes) {
      // Nothing left in our input.
      return;
    }

    // Set up the next control byte.
    control_byte = data[0];
    data++;
    bytes--;
  }
}
