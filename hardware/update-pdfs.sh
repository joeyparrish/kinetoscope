#!/bin/bash

# Kinetoscope: A Sega Genesis Video Player
#
# Copyright (c) 2024 Joey Parrish
#
# See MIT License in LICENSE.txt

# Update schematic PDFs for a project.
# Pass the name of a project without extension, e.g. "cart"

# The PDF from KiCad contains a CreationDate with a second-accurate time.  This
# makes it hard to automate updates to the schematic PDFs because the PDF will
# always change even if the schematic doesn't.
#
# To solve this, we use libfaketime so that KiCad puts a stable date in the PDF.
# If you don't have libfaketime, you'll get a warning from the linker and
# everything else will still work, but today's date and time will appear in the
# PDF.
export LD_PRELOAD=/lib/x86_64-linux-gnu/faketime/libfaketime.so.1
export FAKETIME="1969-07-20 20:17:00"

if ! kicad-cli sch export pdf -o "$1.pdf" "$1.kicad_sch" >/dev/null; then
  echo; echo "Schematic PDF export failed!"
  exit 1
fi

echo "Schematic PDF exported."
