#!/bin/bash

# Kinetoscope: A Sega Genesis Video Player
#
# Copyright (c) 2024 Joey Parrish
#
# See MIT License in LICENSE.txt

# Update schematic PDFs for a project.
# Pass the name of a project without extension, e.g. "cart"

if ! kicad-cli sch export pdf -o "$1.pdf" "$1.kicad_sch" >/dev/null; then
  echo; echo "Schematic PDF export failed!"
  exit 1
fi
echo "Schematic PDF exported."
