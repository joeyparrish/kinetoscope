#!/usr/bin/env python3

# Kinetoscope: A Sega Genesis Video Player
#
# Copyright (c) 2024 Joey Parrish
#
# See MIT License in LICENSE.txt

"""Encode videos into a format appropriate for streaming to a Sega Genesis.

Requires ffmpeg with PNG and PPM output support.

You can fit ~13.6s of audio+video in a 4MB ROM with 128kB left for the player.
"""

import argparse
import glob
import io
import os
import shutil
import subprocess
import sys
import tempfile

from rle_encoder import rle_compress


# A "magic" string in the file header to identify it.
FILE_MAGIC = b"what nintendon't"

# To allow for future changes to the file format, however unlikely it may be
# that people want to expand on this goofy project and worry about
# compatibility, we define a constant for the file format that is written into
# the output file.
FILE_FORMAT = 3

# Number of tiles (w, h) for fullscreen and thumbnail sizes.
FULLSCREEN_TILES = (32, 28)
THUMBNAIL_TILES = (16, 14)

# Maximum number of video index entries.
SEGA_VIDEO_INDEX_MAX_ENTRIES = 36032

# An index offset that indicates EOF.
EOF_OFFSET = 0xffffffff

# Compression constants.
COMPRESSION_NONE = 0
COMPRESSION_RLE = 1

def main(args):
  if args.generate_resource_file and args.compressed:
    print('--generate-resource-file and --compressed are mutually exclusive!')
    sys.exit(1)

  with tempfile.TemporaryDirectory(prefix='encode_sega_video_') as tmp_dir:
    print('Converting {} to {} at {} fps and {} Hz{}.'.format(
        args.input, args.output, args.fps, args.sample_rate,
        ' with compression' if args.compressed else ''))
    print('Temporary files written to {}'.format(tmp_dir))

    # Create various folders within the overall temp dir.
    fullcolor_dir = os.path.join(tmp_dir, 'fullcolor')
    os.mkdir(fullcolor_dir)

    scenes_dir = os.path.join(tmp_dir, 'scenes')
    os.mkdir(scenes_dir)

    quantized_scenes_dir = os.path.join(tmp_dir, 'quantized-scenes')
    os.mkdir(quantized_scenes_dir)

    quantized_dir = os.path.join(tmp_dir, 'quantized')
    os.mkdir(quantized_dir)

    sega_format_dir = os.path.join(tmp_dir, 'sega_format')
    os.mkdir(sega_format_dir)

    thumb_dir = os.path.join(tmp_dir, 'thumb')
    os.mkdir(thumb_dir)

    # Detect crop settings for the input video.
    crop = detect_crop(args)

    # Detect normalization settings for the input audio.
    if args.filter_audio:
      normalization = detect_normalization(args)
    else:
      normalization = None

    # Extract individual frames, reduced to the output framerate, and audio,
    # resampled to the target sample rate and resolution.
    extract_frames_and_audio(args, crop, normalization, fullcolor_dir, tmp_dir)

    # Determine where scene changes are, to optimize the quantization process
    # and improve color quality.
    scenes = detect_scene_changes(args, fullcolor_dir)

    # Organize each scene's frames into a folder.
    construct_scenes(fullcolor_dir, scenes_dir, scenes)

    # Quantize each scene.
    quantize_scenes(args, scenes_dir, quantized_scenes_dir, scenes)

    # Turn those scenes into a single sequence of frames again.
    recombine_scenes(quantized_scenes_dir, quantized_dir)

    # Encode each frame into Sega-formatted tiles.
    encode_frames_to_tiles(quantized_dir, sega_format_dir)

    # Generate a thumbnail image.
    generate_thumbnail(args, fullcolor_dir, thumb_dir)

    # Generate the final output file.
    generate_final_output(args, sega_format_dir, tmp_dir, thumb_dir)

    if args.generate_resource_file:
      generate_resource_file(args)


def run(debug, **kwargs):
  if debug:
    print('+ ' + ' '.join(kwargs['args']))
  return subprocess.run(**kwargs)


def detect_crop(args, skip_keyframes=True):
  rounding = 8  # round to a multiple of 8 pixels, the Sega tile size

  print('Detecting video crop settings...')

  ffmpeg_args = [
    'ffmpeg',
  ]

  if skip_keyframes:
    # Keyframes only.  As much as a 4x speedup on some of my content.
    # Saves ~3 minutes on a full movie.
    ffmpeg_args.extend([
      '-skip_frame', 'nokey',
    ])

  ffmpeg_args.extend([
    # Input.
    '-i', args.input,
    # No audio or sub or metadata processing.
    '-an', '-sn', '-dn',
    # Video filters.
    '-vf', 'cropdetect=round={}'.format(rounding),
  ])

  # Maybe subset the input.
  if args.start:
    ffmpeg_args.extend(['-ss', str(args.start)])
  if args.end:
    ffmpeg_args.extend(['-to', str(args.end)])

  ffmpeg_args.extend([
    # No output.
    '-f', 'null', '-',
  ])

  process = run(args.debug,
      check=True, capture_output=True, text=True, args=ffmpeg_args)

  crop = None
  for line in process.stderr.split('\n'):
    if 'crop=' in line:
      crop = line.split('crop=')[1].split(' ')[0]
      # Do not break.  We get many of these lines, and take from the last.

  if crop is None:
    # We can try again without skipping keyframes.  For some very small videos,
    # this is necessary.
    if skip_keyframes:
      return detect_crop(args, skip_keyframes=False)

    raise RuntimeError(
        'Unable to detect crop settings for {}'.format(args.input))

  print('Cropping with {}'.format(crop))
  return crop


def detect_normalization(args):
  print('Detecting volume normalization...')

  ffmpeg_args = [
    'ffmpeg',
    # Input.
    '-i', args.input,
    # No video or sub or metadata processing.
    '-vn', '-sn', '-dn',
    # Downmix to 1 channel.
    '-ac', '1',
    # Audio filters.
    '-af', 'volumedetect',
  ]

  # Maybe subset the input.
  if args.start:
    ffmpeg_args.extend(['-ss', str(args.start)])
  if args.end:
    ffmpeg_args.extend(['-to', str(args.end)])

  ffmpeg_args.extend([
    # No output.
    '-f', 'null', '-',
  ])

  process = run(args.debug,
      check=True, capture_output=True, text=True, args=ffmpeg_args)

  max_volume = None
  for line in process.stderr.split('\n'):
    # Ex: [Parsed_volumedetect_0 @ 0x55750044e8c0] max_volume: -4.7 dB
    if ' max_volume: ' in line:
      max_volume = line.split(' max_volume: ')[1].split(' ')[0]
      print('Max volume detected: {} dB'.format(max_volume))
      # This only happens once.  Quit parsing.
      break

  if max_volume is None:
    raise RuntimeError(
        'Unable to detect normalization settings for {}'.format(args.input))

  # Whatever it is, invert it. If the max is -4.7, that becomes a 4.7 increase.
  normalization = float(max_volume) * -1

  # Actually, let's target -1.0 dB.  If the source is beyond that, do nothing.
  if normalization <= 1.0:
    normalization = 0
  else:
    normalization -= 1.0

  print('Increasing volume by {:.1f} dB'.format(normalization))
  return normalization


def extract_frames_and_audio(args, crop, normalization, frame_dir, audio_dir):
  # Notes on frame sizing:
  #  - SD analog display (NTSC) is 320x240.
  #  - The player sets the Genesis video processor's (VDP) resolution to
  #    256x224.
  #  - A 320x240 frames squished to 256x224 without regard for aspect ratio
  #    will look right on screen later.
  #  - The VDP works in 8x8 tiles, so 256x224 pixels is 32x28 tiles.

  # Array of ffmpeg filters to use.
  filters = [
    # Crop out blank parts of the input, if any.
    'crop={}'.format(crop),
    # Drop the framerate.
    'fps={}'.format(args.fps),
    # Scale to fit on an SD analog screen.
    'scale=320:240:force_original_aspect_ratio=decrease',
    # Pad to fill that screen.  Yes, this wastes tiles by encoding blank ones.
    # We're fine with that because we intend to stream it.  Who cares about ROM
    # size?
    'pad=320:240:(ow-iw)/2:(oh-ih)/2',
    # Scale it down to the VDP resolution, squishing the frame.
    'scale=256:224',
  ]

  ffmpeg_args = [
    'ffmpeg',
    # Make less noise.
    '-hide_banner', '-loglevel', 'error',
    # But do show progress.
    '-stats',
    # Input.
    '-i', args.input,
    # Video filters.
    '-vf', ','.join(filters),
  ]

  # Maybe subset the video output.
  if args.start:
    ffmpeg_args.extend(['-ss', str(args.start)])
  if args.end:
    ffmpeg_args.extend(['-to', str(args.end)])

  ffmpeg_args.extend([
    # Output specifier for frames.
    os.path.join(frame_dir, 'frame_%05d.png')
  ])

  ffmpeg_args.extend([
    # Mix down to mono audio.
    '-ac', '1',
    # Encode as 8-bit signed raw PCM.
    '-acodec', 'pcm_s8',
    '-f', 's8',
  ])

  if args.filter_audio:
    # Audio filters.
    audio_filters = [
      # Experimentation shows that the biggest source of noise is quantization
      # noise when we go down to 8-bit samples.  This effect is the most
      # extreme in quiet moments, so start by normalizing the volume.
      "volume={}dB".format(normalization),
      # Then, run a denoising filter to remove frequency components below a
      # certain volume threshold.  This part might just be voodoo.  I don't
      # know what I'm doing here, but Star Wars sounds like crap and I'm
      # desperate.
      "afftdn=nr=40:nf=-36",
      # Finally, resample to 13kHz using sox, which should include a low-pass
      # filter to remove frequencies above the Nyquist frequency (13kHz / 2)
      # and avoid aliasing (where high frequencies get mapped to low ones
      # again).
      "aresample={}:resampler=soxr:osf=8:osr={}:dither_method=triangular".format(
          args.sample_rate, args.sample_rate),
    ]

    ffmpeg_args.extend([
      # Add audio filters.
      '-af', ','.join(audio_filters),
    ])
  else:
    ffmpeg_args.extend([
      # Audio sample rate.
      '-ar', str(args.sample_rate),
    ])

  # Apply the same subset to the audio output.
  if args.start:
    ffmpeg_args.extend(['-ss', str(args.start)])
  if args.end:
    ffmpeg_args.extend(['-to', str(args.end)])

  temp_audio_file = os.path.join(audio_dir, 'sound.pcm')
  ffmpeg_args.extend([
    # Output specifier for audio.
    temp_audio_file,
  ])

  print('Extracting video frames and audio...')
  run(args.debug, check=True, args=ffmpeg_args)

  if args.debug_audio:
    audio_debug_path_wav = os.path.join(args.output + '.wav')
    audio_debug_path_pcm = os.path.join(args.output + '.pcm')
    os.makedirs(os.path.dirname(audio_debug_path_wav), exist_ok=True)

    print('Saving extracted audio to {} and {}'.format(
        audio_debug_path_pcm, audio_debug_path_wav))

    shutil.copy(temp_audio_file, audio_debug_path_pcm)

    run(args.debug, check=True, args=[
      'ffmpeg',
      # Make less noise.
      '-hide_banner', '-loglevel', 'error',
      # Input.
      '-f', 's8',
      '-acodec', 'pcm_s8',
      '-ac', '1',
      '-ar', str(args.sample_rate),
      '-i', audio_debug_path_pcm,
      # Output.
      '-acodec', 'pcm_u8',
      '-y', audio_debug_path_wav,
    ])


def detect_scene_changes(args, frame_dir):
  print('Detecting scene changes...')

  ffmpeg_args = [
    'ffmpeg',
    # Input PNGs treated as 1 fps so we can treat PTS as frame number.
    '-r', '1',
    # Input PNGs.
    '-i', os.path.join(frame_dir, 'frame_%05d.png'),
    # Video filters.
    '-vf', "select='gt(scene,{})',showinfo".format(
        args.scene_detection_threshold),
    # No output.
    '-f', 'null', '-',
  ]

  process = run(args.debug,
      check=True, capture_output=True, text=True, args=ffmpeg_args)

  scene_change_frames = []
  for line in process.stderr.split('\n'):
    if 'pts:' in line:
      pts = int(line.split('pts:')[1].strip(' ').split(' ')[0])
      scene_change_frames.append(pts)

  # Each number in scene_changes is a frame number where a scene **ends**.
  scenes = []
  start_frame = 1
  for end_frame in scene_change_frames:
    scenes.append((start_frame, end_frame))
    start_frame = end_frame + 1

  # The frame numbers are 1-based, so num_inputs is also the final frame number.
  num_inputs = len(glob.glob(os.path.join(frame_dir, '*.png')))
  scenes.append((start_frame, num_inputs))

  return scenes


def construct_scenes(input_dir, output_dir, scenes):
  scene_index = 0

  for start_frame, end_frame in scenes:
    scene_dir = os.path.join(output_dir, 'scene_{:05d}'.format(scene_index))
    os.makedirs(scene_dir)

    for input_num in range(start_frame, end_frame + 1):
      # Maintain the same frame numbers when splitting.
      frame_name = 'frame_{:05d}.png'.format(input_num)
      input_frame = os.path.join(input_dir, frame_name)
      output_frame = os.path.join(scene_dir, frame_name)
      shutil.copy(input_frame, output_frame)

    scene_index += 1
    print('\rCreated {} / {} scenes...'.format(scene_index, len(scenes)),
          end='')

  print('')


def quantize_scene(args, input_scene_dir, output_scene_dir, start_frame):
  # Create an optimized palette first.
  output_pal_path = os.path.join(output_scene_dir, 'pal.png')

  # Color quantization formula that reduces 8-bit colors to 3-bit colors,
  # scaled back up to 8-bit representation.
  # 255 => 256 => 8 => 256 => 255
  formula='(32 * floor((val + 1)/32)) - 1'
  filters = [
    # Reduce color complexity to that representable by the Sega, 4 bits per
    # pixel.  Doing this before palette generation avoids allocating multiple
    # palette slots to colors that map to the same 4-bit color later in a
    # Sega-specific tile format.
    'lut=r={}:g={}:b={}'.format(formula, formula, formula),
    # Compute an optimized 15-color palette (16 color palette, but color 0 is
    # always treated as transparent), based on the reduced color depth from the
    # previous filter.
    'palettegen=max_colors=16:reserve_transparent=1',
  ]

  ffmpeg_args = [
    'ffmpeg',
    # Make no noise, except on error.
    '-hide_banner', '-loglevel', 'error', '-nostats',
    # Input and starting frame number.
    '-start_number', str(start_frame),
    '-i', os.path.join(input_scene_dir, 'frame_%05d.png'),
    # Video filters from above.
    '-vf', ','.join(filters),
    # Output a palette image.
    output_pal_path,
  ]
  run(args.debug, check=True, args=ffmpeg_args)

  ffmpeg_args = [
    'ffmpeg',
    # Make no noise, except on error.
    '-hide_banner', '-loglevel', 'error', '-nostats',
    # Input and starting frame number.
    '-start_number', str(start_frame),
    '-i', os.path.join(input_scene_dir, 'frame_%05d.png'),
    # Palette.
    '-i', output_pal_path,
    # Use the optimized palette to quantize all the frames in the scene.
    '-lavfi', 'paletteuse=dither={}'.format(args.dithering),
    # Output individual frames in PPM format with the same frame numbers.
    '-start_number', str(start_frame),
    os.path.join(output_scene_dir, 'frame_%05d.ppm'),
  ]
  run(args.debug, check=True, args=ffmpeg_args)


def quantize_scenes(args, input_dir, output_dir, scenes):
  scene_paths = sorted(glob.glob(os.path.join(input_dir, '*')))
  scene_index = 0

  while scene_index < len(scenes):
    start_frame, end_frame = scenes[scene_index]
    input_scene_dir = scene_paths[scene_index]
    scene_name = os.path.basename(input_scene_dir)

    output_scene_dir = os.path.join(output_dir, scene_name)
    os.makedirs(output_scene_dir)

    quantize_scene(args, input_scene_dir, output_scene_dir, start_frame)

    scene_index += 1
    print('\rQuantized {} / {} scenes...'.format(scene_index, len(scenes)),
          end='')
    if args.debug: print('')

  if not args.debug: print('')


def recombine_scenes(input_dir, output_dir):
  # Since we maintained frame numbers during the split and quantization
  # process, we simply combine the contents of all the input directories into
  # the output directory.
  scene_paths = glob.glob(os.path.join(input_dir, '*'))
  for input_scene_dir in scene_paths:
    for input_frame in glob.glob(os.path.join(input_scene_dir, '*.ppm')):
      shutil.move(input_frame, output_dir)


def encode_frames_to_tiles(input_dir, output_dir):
  all_inputs = glob.glob(os.path.join(input_dir, '*.ppm'))
  count = 0

  for input_path in all_inputs:
    input_filename = os.path.basename(input_path)
    output_filename = input_filename.replace('.ppm', '.bin')
    output_path = os.path.join(output_dir, output_filename)

    ppm_to_sega_frame(input_path, output_path, FULLSCREEN_TILES)

    count += 1
    print('\rConverted {} / {} frames to tiles...'.format(
          count, len(all_inputs)), end='')
  print('')


def ppm_to_sega_frame(in_path, out_path, expected_tiles):
  with open(in_path, 'rb') as f:
    data = f.read()

  # Parse the PPM header.
  header = data.split(b'\n')[0:3]
  magic, size, _ = header
  assert(magic == b'P6')

  width, height = map(int, size.split(b' '))
  header_size = len(b'\n'.join(header)) + 1 # final newline

  # Extract pixel data.
  data = data[header_size:]

  # Entry 0 is always transparent when rendered.  We store black there.
  # If another index is assigned black, that one will be opaque.
  palette = [0x000]

  # Each tile is 8x8 pixels, 4 bit palette index per pixel.
  binary_tiles = b''

  # The image dimensions should each be a multiple of 8 already.
  assert width % 8 == 0 and height % 8 == 0
  tiles_width = width // 8
  tiles_height = height // 8

  # We should have a fullscreen image.
  assert (tiles_width, tiles_height) == expected_tiles

  for tile_y in range(tiles_height):
    for tile_x in range(tiles_width):
      tile = []

      for y in range(8):
        for x in range(8):
          pixel_index = ((tile_y * 8) + y) * width + (tile_x * 8) + x
          data_index = pixel_index * 3
          r, g, b = data[data_index:data_index+3]
          sega_color = rgb_to_sega_color(r, g, b)

          if sega_color in palette:
            palette_index = palette.index(sega_color)
          else:
            palette_index = len(palette)
            palette.append(sega_color)

          tile.append(palette_index)

      binary_tiles += pack_tile(tile)

  with open(out_path, 'wb') as f:
    # SegaVideoFrameHeader contains the palette only
    f.write(pack_palette(palette))
    # Actual tile data follows
    f.write(binary_tiles)


def rgb_to_sega_color(r, g, b):
  # We only get 3 bits of accuracy for each for red, green, and blue.
  r //= 32
  g //= 32
  b //= 32
  # These get put into 4-bit fields in memory, ABGR, in a u16.
  return (b << 9) | (g << 5) | (r << 1)


def pack_tile(palette_indexes):
  packed = b''
  for i in range(32):
    first, second = palette_indexes[2*i : 2*i + 2]
    packed += ((first << 4) | second).to_bytes(1, 'big')
  return packed


def pack_palette(palette):
  packed = b''
  for i in range(16):  # palette may be smaller...
    c = palette[i] if i < len(palette) else 0
    packed += c.to_bytes(2, 'big')
  return packed


def patch_at_offset(f, patch_offset, value, size):
  offset = f.tell()
  f.seek(patch_offset)
  if type(value) == list:
    for item in value:
      f.write(item.to_bytes(size, 'big'))
  else:
    f.write(value.to_bytes(size, 'big'))
  f.seek(offset)


class ChunkWritingState(object):
  sound_file = None
  samples_per_chunk = 0
  frames_per_chunk = 0
  frame_paths = []
  chunk_size = 0
  num_chunks = 0
  sound_len = 0  # bytes left to write
  frame_count = 0  # frames left to write
  frame_path_index = 0  # next index into frame_paths


def write_chunk(f, state):
  # Write SegaVideoChunkHeader
  start_of_chunk = f.tell()
  chunk_sound_size = min(state.sound_len, state.samples_per_chunk)
  f.write(chunk_sound_size.to_bytes(4, 'big'))
  chunk_frame_count = min(state.frame_count, state.frames_per_chunk)
  f.write(chunk_frame_count.to_bytes(2, 'big'))
  f.write(bytes(2))  # "unused1", formerly "finalChunk"

  current_offset = f.tell()
  pre_padding_remainder = (current_offset + 4) % 256
  pre_padding_bytes = 256 - pre_padding_remainder if pre_padding_remainder else 0
  f.write(pre_padding_bytes.to_bytes(2, 'big'))

  # We fill this in later.
  post_padding_bytes = 0
  post_padding_bytes_offset = f.tell()
  f.write(post_padding_bytes.to_bytes(2, 'big'))

  # Add pre-padding.
  f.write(bytes(pre_padding_bytes))

  # Write audio:
  sound_data = state.sound_file.read(chunk_sound_size)
  if len(sound_data) < chunk_sound_size:
    # Padding up to sound alignment requirements
    sound_data += bytes(chunk_sound_size - len(sound_data))
    assert len(sound_data) == chunk_sound_size
  f.write(sound_data)
  state.sound_len -= chunk_sound_size

  # Write frames:
  chunk_frame_data_len = 0
  for i in range(chunk_frame_count):
    with open(state.frame_paths[state.frame_path_index], 'rb') as frame_file:
      frame_data = frame_file.read()
      f.write(frame_data)
      chunk_frame_data_len += len(frame_data)
    state.frame_count -= 1
    state.frame_path_index += 1

  # Figure out the post-padding.
  end_of_frames = f.tell()
  post_padding_remainder = end_of_frames % 256
  post_padding_bytes = 256 - post_padding_remainder if post_padding_remainder else 0

  # Seek back to fill in the post-padding field.
  patch_at_offset(f, post_padding_bytes_offset, post_padding_bytes, 2)

  # Add post-padding.
  f.write(bytes(post_padding_bytes))

  # If this is the first chunk, record the size.
  end_of_chunk = f.tell()
  if state.chunk_size == 0:
    state.chunk_size = end_of_chunk - start_of_chunk

  # Count chunks.
  state.num_chunks += 1


def compress(compression, uncompressed):
  if compression == COMPRESSION_NONE:
    return uncompressed

  if compression == COMPRESSION_RLE:
    return rle_compress(uncompressed)

  raise RuntimeError('Unrecognized compression constant')


def generate_final_output(args, frame_dir, sound_dir, thumb_dir):
  print('Generating final output {}...'.format(args.output))

  sound_path = os.path.join(sound_dir, 'sound.pcm')
  raw_sound_len = os.path.getsize(sound_path)
  state = ChunkWritingState()

  # Pad sound up to a 256-byte multiple as required by the driver:
  sound_remainder = raw_sound_len % 256
  sound_padding = (256 - sound_remainder) if sound_remainder else 0
  state.sound_len = raw_sound_len + sound_padding
  assert state.sound_len % 256 == 0

  # Compute chunk sizes
  state.samples_per_chunk = args.sample_rate * args.chunk_length
  state.frames_per_chunk = args.fps * args.chunk_length

  # List all frames:
  state.frame_paths = sorted(glob.glob(os.path.join(frame_dir, '*.bin')))
  state.frame_count = len(state.frame_paths)

  state.frame_path_index = 0

  # Index of compressed chunk offsets.
  index = [ EOF_OFFSET ] * SEGA_VIDEO_INDEX_MAX_ENTRIES

  # Create the output folder.
  output_folder = os.path.dirname(args.output)
  if output_folder:
    os.makedirs(output_folder, exist_ok=True)

  with open(sound_path, 'rb') as sound_file:
    with open(args.output, 'wb') as f:
      state.sound_file = sound_file
      state.chunk_size = 0
      state.num_chunks = 0

      # Write SegaVideoHeader
      f.write(FILE_MAGIC)
      f.write(FILE_FORMAT.to_bytes(2, 'big'))
      f.write(args.fps.to_bytes(2, 'big'))
      f.write(args.sample_rate.to_bytes(2, 'big'))
      f.write(state.frame_count.to_bytes(4, 'big'))
      f.write(state.sound_len.to_bytes(4, 'big'))
      chunk_size_offset = f.tell()
      f.write(state.chunk_size.to_bytes(4, 'big'))
      f.write(state.num_chunks.to_bytes(4, 'big'))

      # Compute the title for the metadata, truncate/pad to 128 bytes including
      # terminator.
      title = os.path.splitext(os.path.basename(args.input))[0]
      title = args.title or title
      title = title.encode('utf-8')
      title = (title + bytes(128))[0:127] + b'\0'
      assert len(title) == 128
      f.write(title)

      f.write(bytes(128)) # relative URL, filled in for catalog later

      compression = COMPRESSION_RLE if args.compressed else COMPRESSION_NONE
      f.write(compression.to_bytes(2, 'big'))

      f.write(bytes(696)) # Padding/unused

      with open(os.path.join(thumb_dir, 'thumb.segaframe'), 'rb') as thumb:
        f.write(thumb.read())
      # End of SegaVideoHeader

      if args.compressed:
        # Write SegaVideoIndex (empty for now, will rewrite later)
        video_index_offset = f.tell()
        for offset in index:
          f.write(offset.to_bytes(4, 'big'))

      total_frames = state.frame_count
      while state.sound_len and state.frame_count:
        if args.compressed:
          # Minus one here because we need the final entry for the total size.
          if state.num_chunks >= SEGA_VIDEO_INDEX_MAX_ENTRIES - 1:
            raise RuntimeError('Streaming index overflow!')
          index[state.num_chunks] = f.tell()

          f2 = io.BytesIO()
          write_chunk(f2, state)
          f2.seek(0)
          uncompressed = f2.read()

          compressed = compress(compression, uncompressed)
          f.write(compressed)
        else:
          write_chunk(f, state)

        print('\rOutput {} / {} frames...'.format(
            total_frames - state.frame_count, total_frames), end='')

      print('')

      # Seek back to the header to fill in these two fields.
      patch_at_offset(f, chunk_size_offset, state.chunk_size, 4)
      patch_at_offset(f, chunk_size_offset + 4, state.num_chunks, 4)

      if args.compressed:
        # Seek back to fill in the index.
        index[state.num_chunks] = f.tell()
        patch_at_offset(f, video_index_offset, index, 4)

  print('Output complete.')


def generate_resource_file(args):
  # Create a resource file next to the output file.
  # If the output is "my-video.segavideo", the resource file will be
  # "my-video.res", it will be included in your project with "my-video.h", and
  # the data referenced with the pointer "my_video".
  output_dir = os.path.dirname(args.output)
  output_filename = os.path.basename(args.output)
  output_name = os.path.splitext(output_filename)[0]
  output_variable_name = output_name.replace('-', '_')
  resource_file_path = os.path.join(output_dir, output_name + '.res')

  with open(resource_file_path, 'w') as f:
    f.write('BIN {} {} 256\n'.format(
        output_variable_name, output_filename))

  print('Resource file {} generated.'.format(resource_file_path))
  print('Include in your project via "{}.h"'
        ' and use the pointer "{}".'.format(output_name, output_variable_name))


def generate_thumbnail(args, fullcolor_dir, thumb_dir):
  # Choose a thumbnail frame by fraction through the video.
  fullcolor_frames = sorted(glob.glob(os.path.join(fullcolor_dir, '*.png')))
  thumb_index = int(len(fullcolor_frames) * args.thumbnail_fraction)
  fullcolor_thumb_frame = fullcolor_frames[thumb_index]

  # Set up what quantize_scene expects for input and output.
  thumb_in_dir = os.path.join(thumb_dir, 'in')
  thumb_out_dir = os.path.join(thumb_dir, 'out')
  os.mkdir(thumb_in_dir)
  os.mkdir(thumb_out_dir)

  thumb_in = os.path.join(thumb_in_dir, 'frame_00001.png')
  thumb_out = os.path.join(thumb_out_dir, 'frame_00001.ppm')
  sega_frame_out = os.path.join(thumb_dir, 'thumb.segaframe')

  # Create a half-sized version of the frame to quantize and convert to a
  # Sega-compatible format.
  ffmpeg_args = [
    'ffmpeg',
    # Make no noise, except on error.
    '-hide_banner', '-loglevel', 'error', '-nostats',
    # Input.
    '-i', fullcolor_thumb_frame,
    # Scale.
    '-vf', 'scale=128:112',
    # Output.
    thumb_in,
  ]
  run(args.debug, check=True, args=ffmpeg_args)

  # Now quantize this half-sized image.
  quantize_scene(args, thumb_in_dir, thumb_out_dir, 1)

  # Then convert to Sega format.
  ppm_to_sega_frame(thumb_out, sega_frame_out, THUMBNAIL_TILES)

  print('Thumbnail generated from frame #{}.'.format(thumb_index + 1))


if __name__ == '__main__':
  prog = os.path.basename(sys.argv[0])
  description = __doc__

  parser = argparse.ArgumentParser(
      formatter_class=argparse.RawDescriptionHelpFormatter,
      prog=prog,
      description=description)

  parser.add_argument('-i', '--input',
      required=True,
      help='Input file to encode. Should have one video and one audio stream.')
  parser.add_argument('-s', '--start',
      type=float,
      default=0,
      help='Starting time in seconds.  Use to make a short clip.')
  parser.add_argument('-e', '--end',
      type=float,
      default=None,
      help='Ending time in seconds.  Use to make a short clip.')
  parser.add_argument('-o', '--output',
      required=True,
      help='Output file.')
  parser.add_argument('-g', '--generate-resource-file',
      action='store_true',
      help='Generate SGDK resource file for hard-coding a video into a ROM.')
  parser.add_argument('-z', '--compressed',
      action='store_true',
      help='Compress chunks.  Incompatible with embedded playback (-g).')
  parser.add_argument('-t', '--title',
      help='Title to store in metadata.  Defaults to input filename.')
  parser.add_argument('-f', '--fps',
      type=int,
      default=10,
      help='Video frame rate in frames per second.'
           ' Each frame consumes 28kB.')
  parser.add_argument('-r', '--sample-rate',
      type=int,
      default=13312,
      help='Audio sample rate in Hz.'
           ' One second of audio occupies (rate) bytes.'
           " The default is the exact rate used by the player's audio driver."
           ' You should not change it.')
  parser.add_argument('-c', '--chunk-length',
      type=int,
      default=3,
      help='Chunk length in seconds.'
           ' Chunks should fit in 1MB or less with all headers.')
  parser.add_argument('--thumbnail-fraction',
      type=float,
      default=2/3,
      help='Fraction of the way through the video to choose a thumbnail.')
  parser.add_argument('--scene-detection-threshold',
      type=float,
      default=0.5,
      help='The percentage of pixels that must change to detect a new scene.'
           ' This is applied after padding and scaling, so depending on the'
           ' input resolution, this may need to be tweaked. For vertical'
           ' video, we recommend something more like 0.2 because so many of'
           ' the pixels will be occupied by black padding. To generate a'
           ' unique palette per frame instead of per scene, set to 0.')
  parser.add_argument('--dithering',
      default='bayer',
      help='The ffmpeg dithering algorithm to use.'
           ' The default of "bayer" produces good results, but you may prefer'
           ' "none" for some content.'
           ' See https://ffmpeg.org/ffmpeg-filters.html#paletteuse for a full'
           ' list of options.')
  parser.add_argument('--no-filter-audio',
      dest='filter_audio',
      action='store_false',
      help='Skip audio filtering and normalization.')
  parser.add_argument('--debug',
      action='store_true',
      help='Print all ffmpeg commands.')
  parser.add_argument('--debug-audio',
      action='store_true',
      help='Save 8-bit audio for debugging filtering and audio driver.')

  args = parser.parse_args()
  main(args)
