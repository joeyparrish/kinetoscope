#!/usr/bin/env python3

# Kinetoscope: A Sega Genesis Video Player
#
# Copyright (c) 2024 Joey Parrish
#
# See MIT License in LICENSE.txt

"""Encode videos into a format appropriate for streaming to a Sega Genesis.

Requires: ImageMagick, ffmpeg with PNG output support.

You can fit ~13.6s of audio+video in a 4MB ROM with 128kB left for the player.
"""

import argparse
import glob
import os
import subprocess
import sys
import tempfile


# A "magic" string in the file header to identify it.
FILE_MAGIC = b"what nintendon't"

# To allow for future changes to the file format, however unlikely it may be
# that people want to expand on this goofy project and worry about
# compatibility, we define a constant for the file format that is written into
# the output file.
FILE_FORMAT = 1

# To deal with very large images, we may need to override the ImageMagick
# policies on maximum image size and/or memory usage.
IMAGEMAGICK_LARGE_IMAGE_ENV = {
  'MAGICK_CONFIGURE_PATH': os.path.join(
      os.path.dirname(__file__), 'imagemagick-policy'),
}


def main(args):
  with tempfile.TemporaryDirectory(prefix='encode_sega_video_') as tmp_dir:
    print('Converting {} to {} at {} fps and {} Hz.'.format(
        args.input, args.output, args.fps, args.sample_rate))
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

    # Detect crop settings for the input video.
    crop = detect_crop(args)

    # Extract individual frames, reduced to the output framerate, and audio,
    # resampled to the target sample rate and resolution.
    extract_frames_and_audio(args, crop, fullcolor_dir, tmp_dir)

    if args.detect_scenes:
      # Determine where scene changes are, to optimize the quantization process
      # and improve color quality.
      scenes = detect_scene_changes(args, fullcolor_dir)

      # Construct single images per scene for quantization.
      construct_scenes(fullcolor_dir, scenes_dir, scenes)

      # Quantize each scene.
      quantize_frames(scenes_dir, quantized_scenes_dir, scenes=True)

      # Split those scenes into individual frames again.
      split_scenes(quantized_scenes_dir, quantized_dir, scenes)
    else:
      # Quantize each frame.
      quantize_frames(fullcolor_dir, quantized_dir, scenes=False)

    # Encode each frame into Sega-formatted tiles.
    encode_frames_to_tiles(quantized_dir, sega_format_dir)

    # Generate the final output file.
    generate_final_output(args, sega_format_dir, tmp_dir)

    if args.generate_resource_file:
      generate_resource_file(args)


def detect_crop(args):
  rounding = 8  # round to a multiple of 8 pixels, the Sega tile size

  print('Detecting video crop settings...')

  ffmpeg_args = [
    'ffmpeg',
    # Input.
    '-i', args.input,
    # Video filters.
    '-vf', 'cropdetect=round={}'.format(rounding),
    # No output.
    '-f', 'null', '-',
  ]

  process = subprocess.run(
      check=True, capture_output=True, text=True, args=ffmpeg_args)

  crop = None
  for line in process.stderr.split('\n'):
    if 'crop=' in line:
      crop = line.split('crop=')[1].split(' ')[0]

  if crop is None:
    raise RuntimeError(
        'Unable to detect crop settings for {}'.format(args.input))

  return crop


def extract_frames_and_audio(args, crop, frame_dir, audio_dir):
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
    # Audio sample rate.
    '-ar', str(args.sample_rate),
    # Mix down to mono audio.
    '-ac', '1',
    # Encode as 8-bit signed raw PCM.
    '-acodec', 'pcm_s8',
    '-f', 's8',
  ])

  # Apply the same subset to the audio output.
  if args.start:
    ffmpeg_args.extend(['-ss', str(args.start)])
  if args.end:
    ffmpeg_args.extend(['-to', str(args.end)])

  ffmpeg_args.extend([
    # Output specifier for audio.
    os.path.join(audio_dir, 'sound.pcm'),
  ])

  print('Extracting video frames and audio...')
  subprocess.run(check=True, args=ffmpeg_args)


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

  process = subprocess.run(
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
    # Using ImageMagick to combine frames vertically into one long scene.
    args = ['convert', '-append']

    # The input frames for this scene.
    for num in range(start_frame, end_frame + 1):
      args.append(os.path.join(input_dir, 'frame_{:05d}.png'.format(num)))

    # The output for this scene.
    args.append(
        os.path.join(output_dir, 'scene_{:05d}.png'.format(scene_index)))

    subprocess.run(check=True, args=args, env=IMAGEMAGICK_LARGE_IMAGE_ENV)

    scene_index += 1
    print('\rCreated {} / {} scenes...'.format(scene_index, len(scenes)),
          end='')

  print('')


def quantize_frames(input_dir, output_dir, scenes):
  all_inputs = glob.glob(os.path.join(input_dir, '*.png'))
  count = 0
  thing = 'scene' if scenes else 'frame'

  for input_path in all_inputs:
    input_filename = os.path.basename(input_path)
    output_filename = input_filename.replace('.png', '.ppm')
    output_path = os.path.join(output_dir, output_filename)

    args = [
      # Convert tool from ImageMagick.
      'convert',
      # Input.
      input_path,
      # Disable dithering.
      '+dither',
      # Quantize to 15 colors (16 color palette, but color 0 is always treated
      # as transparent).
      '-colors', '15',
      # Output.
      output_path,
    ]

    subprocess.run(check=True, args=args, env=IMAGEMAGICK_LARGE_IMAGE_ENV)

    count += 1
    print('\rQuantized {} / {} {}s...'.format(count, len(all_inputs), thing),
          end='')
  print('')


def split_scenes(input_dir, output_dir, scenes):
  scene_index = 0
  while scene_index < len(scenes):
    start_frame, end_frame = scenes[scene_index]
    scene_size = end_frame - start_frame + 1
    scene_path = os.path.join(input_dir, 'scene_{:05d}.ppm'.format(scene_index))
    # Not filled by us, sent directly to ImageMagick.
    output_template = os.path.join(output_dir, 'frame_%05d.ppm')

    # Use ImageMagick to split the scene (verticaly) into frames.
    args = [
      'convert',
      scene_path,
      '+repage',
      '-crop', '256x224',
      '-scene', str(start_frame),
      output_template,
    ]

    subprocess.run(check=True, args=args, env=IMAGEMAGICK_LARGE_IMAGE_ENV)

    scene_index += 1
    print('\rSplit {} / {} scenes...'.format(scene_index, len(scenes)),
          end='')
  print('')


def encode_frames_to_tiles(input_dir, output_dir):
  all_inputs = glob.glob(os.path.join(input_dir, '*.ppm'))
  count = 0

  for input_path in all_inputs:
    input_filename = os.path.basename(input_path)
    output_filename = input_filename.replace('.ppm', '.bin')
    output_path = os.path.join(output_dir, output_filename)

    ppm_to_sega_frame(input_path, output_path)

    count += 1
    print('\rConverted {} / {} frames to tiles...'.format(
          count, len(all_inputs)), end='')
  print('')


def ppm_to_sega_frame(in_path, out_path):
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
  assert tiles_width == 32
  assert tiles_height == 28

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
  # 4 bits each for red, green, and blue, packed into a u16.
  r //= 16
  g //= 16
  b //= 16
  return (b << 8) | (g << 4) | r


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


def generate_final_output(args, frame_dir, sound_dir):
  print('Generating final output {}...'.format(args.output))

  sound_path = os.path.join(sound_dir, 'sound.pcm')
  raw_sound_len = os.path.getsize(sound_path)

  # Pad sound up to a 256-byte multiple as required by the driver:
  sound_remainder = raw_sound_len % 256
  sound_padding = (256 - sound_remainder) if sound_remainder else 0
  sound_len = raw_sound_len + sound_padding
  assert sound_len % 256 == 0

  # Compute chunk sizes
  samples_per_chunk = args.sample_rate * args.chunk_length
  frames_per_chunk = args.fps * args.chunk_length

  # List all frames:
  frame_paths = sorted(glob.glob(os.path.join(frame_dir, '*.bin')))
  frame_count = len(frame_paths)

  frame_path_index = 0

  # Create the output folder.
  output_folder = os.path.dirname(args.output)
  if output_folder:
    os.makedirs(output_folder, exist_ok=True)

  with open(sound_path, 'rb') as sound_file:
    with open(args.output, 'wb') as f:
      # Write SegaVideoHeader
      f.write(FILE_MAGIC)
      f.write(FILE_FORMAT.to_bytes(2, 'big'))
      f.write(args.fps.to_bytes(2, 'big'))
      f.write(args.sample_rate.to_bytes(2, 'big'))
      f.write(frame_count.to_bytes(2, 'big'))
      f.write(sound_len.to_bytes(4, 'big'))

      while sound_len and frame_count:
        # Write SegaVideoChunkHeader
        chunk_sound_size = min(sound_len, samples_per_chunk)
        f.write(chunk_sound_size.to_bytes(4, 'big'))
        chunk_frame_count = min(frame_count, frames_per_chunk)
        f.write(chunk_frame_count.to_bytes(2, 'big'))
        current_position = f.tell()
        padding_remainder = (current_position + 2) % 256
        padding_bytes = 256 - padding_remainder if padding_remainder else 0
        f.write(padding_bytes.to_bytes(2, 'big'))
        f.write(bytes(padding_bytes))

        # Write audio:
        sound_data = sound_file.read(chunk_sound_size)
        if len(sound_data) < chunk_sound_size:
          # Padding up to sound alignment requirements
          sound_data += bytes(chunk_sound_size - len(sound_data))
          assert len(sound_data) == chunk_sound_size
        f.write(sound_data)
        sound_len -= chunk_sound_size

        # Write frames:
        chunk_frame_data_len = 0
        for i in range(chunk_frame_count):
          with open(frame_paths[frame_path_index], 'rb') as frame_file:
            frame_data = frame_file.read()
            f.write(frame_data)
            chunk_frame_data_len += len(frame_data)
          frame_count -= 1
          frame_path_index += 1

      # Write final SegaVideoChunkHeader to indicate no more chunks
      chunk_sound_size = 0
      f.write(chunk_sound_size.to_bytes(4, 'big'))
      chunk_frame_count = 0
      f.write(chunk_frame_count.to_bytes(2, 'big'))
      padding_bytes = 0
      f.write(padding_bytes.to_bytes(2, 'big'))

  print('Output complete.')


def generate_resource_file(args):
  # Create a resource file next to the output file.
  # If the output is "my-video.bin", the resource file will be "my-video.res",
  # it will be included in your project with "my-video.h", and the data
  # referenced with the pointer "my_video".
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
  parser.add_argument('-d', '--detect-scenes',
      action='store_true',
      help='Use scene-detection to optimize color quantization.'
           ' This reduces flickering colors by quantizing entire scenes'
           ' instead of individual frames.')
  parser.add_argument('--scene-detection-threshold',
      type=float,
      default=0.5,
      help='The percentage of pixels that must change to detect a new scene.'
           ' This is applied after padding and scaling, so depending on the'
           ' input resolution, this may need to be tweaked. For vertical'
           ' video, we recommend something more like 0.2 because so many of'
           ' the pixels will be occupied by black padding.')

  args = parser.parse_args()
  main(args)
