#!/usr/bin/env python3

import csv
import sys

# Packages we know need rotation.
ROTATE_PACKAGES = {
  'LQFP-48_7x7mm_P0.5mm': -90,
  'SO-16_3.9x9.9mm_P1.27mm': -90,
  'SSOP-24_5.3x8.2mm_P0.65mm': -90,
  'TO-252-3_TabPin2': 180,
  'TSSOP-16_4.4x3.6mm_P0.4mm': -90,
  'TSSOP-16_4.4x5mm_P0.65mm': -90,
  'VSSOP-8_2.3x2mm_P0.5mm': -90,
}

# Packages we know don't need rotation.
OKAY_PACKAGES = [
  'C_0603_1608Metric',
  'C_1206_3216Metric',
  'Crystal_SMD_0603-2Pin_6.0x3.5mm',
  'L_0603_1608Metric',
  'R_0603_1608Metric',
  'TSOP-I-48_18.4x12mm_P0.5mm',
]

# Packages JLCPCB doesn't have at all AFAICT.
DONT_HAVE_PACKAGES = [
  'CP_Radial_D5.0mm_P2.00mm',
  'PinHeader_2x26_P2.54mm_Vertical',
  'TO-220-3_Horizontal_TabDown',
]

bom_in, bom_out, pos_in, pos_out = sys.argv[1:]

with open(bom_in, 'r', newline='') as f:
  reader = csv.DictReader(f)
  bom_fields = reader.fieldnames
  bom_rows = [row for row in reader]

with open(pos_in, 'r', newline='') as f:
  reader = csv.DictReader(f)
  pos_fields = reader.fieldnames
  pos_rows = [row for row in reader]

skipped_names = []
with open(bom_out, 'w', newline='') as f:
  writer = csv.DictWriter(f, fieldnames=bom_fields)
  writer.writeheader()

  for row in bom_rows:
    part_num = row['JLCPCB Part#']
    name = row['Designator']

    # Only write rows we have part numbers for.  The others, we don't expect
    # JLCPCB to populate.  They will sometimes guess a part number for us, but
    # it's never right, so it's best to skip these lines.
    if part_num:
      writer.writerow(row)
    else:
      print('Skipping {}, no part number'.format(name))
      skipped_names.extend(name.split(','))

with open(pos_out, 'w', newline='') as f:
  writer = csv.DictWriter(f, fieldnames=pos_fields)
  writer.writeheader()

  for row in pos_rows:
    package = row['Package']
    name = row['Designator']
    rotation = float(row['Rotation'])

    # Skip anything we omitted from the BOM.
    if name in skipped_names:
      continue

    # Correct the rotation of certain items.
    if package in ROTATE_PACKAGES:
      print('Fixing rotation of {}, package {}'.format(name, package))
      rotation += ROTATE_PACKAGES[package]
      while rotation < 0: rotation += 360
      while rotation >= 360: rotation -= 360
      row['Rotation'] = '{:.6f}'.format(rotation)
    elif package in OKAY_PACKAGES or package in DONT_HAVE_PACKAGES:
      pass
    else:
      print('Unsure about rotation of {}, package {}'.format(name, package))

    writer.writerow(row)
