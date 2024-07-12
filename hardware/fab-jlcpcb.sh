#!/bin/bash

# Kinetoscope: A Sega Genesis Video Player
#
# Copyright (c) 2024 Joey Parrish
#
# See MIT License in LICENSE.txt

# Prepare a project for fabrication at JLCPCB.
# Pass the name of a project without extension, e.g. "cart"
rm -rf "$1-fab/" "$1-fab.zip"

# Make sure we're passing all checks first.
echo "Running ERC..."
rm -f "$1.rpt"
if ! kicad-cli sch erc --exit-code-violations "$1.kicad_sch" >/dev/null; then
  [[ -e "$1.rpt" ]] && cat "$1.rpt"
  echo; echo "ERC failed!"
  exit 1
fi

echo "Running DRC..."
rm -f "$1.rpt"
if ! kicad-cli pcb drc --exit-code-violations "$1.kicad_pcb" >/dev/null; then
  [[ -e "$1.rpt" ]] && cat "$1.rpt"
  echo; echo "DRC failed!"
  exit 1
fi

mkdir "$1-fab/"

echo "Exporting gerbers..."
LAYERS="F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts,F.Fab"
# Options based on https://jlcpcb.com/help/article/362-how-to-generate-gerber-and-drill-files-in-kicad-7
# plus the experience that use you _should_ check "use drill/place file
# origin", which JLCPCB's docs do not show checked in their screenshot.
if ! kicad-cli pcb export gerbers \
    -l "$LAYERS" \
    --exclude-value \
    --no-x2 \
    --no-netlist \
    --subtract-soldermask \
    --use-drill-file-origin \
    -o "$1-fab/" "$1.kicad_pcb"; then
  echo; echo "Failed to export gerbers!"
  exit 1
fi

echo "Exporting drill files..."
if ! kicad-cli pcb export drill \
    --format excellon \
    --excellon-units mm \
    --excellon-zeros-format decimal \
    --excellon-oval-format alternate \
    --excellon-separate-th \
    --drill-origin absolute \
    --generate-map \
    --map-format gerberx2 \
    -o "$1-fab/" "$1.kicad_pcb"; then
  echo; echo "Failed to export drill files!"
  exit 1
fi

echo "Exporting position files..."
# NOTE: I have avoided SMT components on the bottom, so I'm just exporting
# top-side position files.  I don't know if the --bottom-negate-x option would
# be necessary or not for bottom placement if you wanted it.
if ! kicad-cli pcb export pos \
    --side front \
    --format csv \
    --units mm \
    --use-drill-file-origin \
    --exclude-dnp \
    -o "$1-fab/$1-front.pos.csv" "$1.kicad_pcb"; then
  echo; echo "Failed to export position files!"
  exit 1
fi

# Modify KiCad's CSV position files for JLCPCB by renaming the columns.
echo "Translating position file columns for JLCPCB..."
if ! sed \
    -e 's/Ref/Designator/' \
    -e 's/Pos\([XY]\)/"Mid \1"/g' \
    -e 's/\<Rot\>/Rotation/' \
    -e 's/Side/Layer/' -i "$1-fab/$1-front.pos.csv"; then
  echo; echo "Failed to translate position file columns for JLCPCB!"
  exit 1
fi

echo "Exporting BOM..."
GROUP_BY="Value,Footprint"
FIELDS="Reference,Value,Footprint,\${QUANTITY},Part Number,JLCPCB Part Number"
LABELS="Designator,Comment,Footprint,Quantity,Part#,JLCPCB Part#"
SORT_BY="Designator"
if ! kicad-cli sch export bom \
    --fields "$FIELDS" \
    --labels "$LABELS" \
    --group-by "$GROUP_BY" \
    --sort-field "$SORT_BY" \
    --ref-range-delimiter "" \
    --exclude-dnp \
    -o "$1-fab/$1.bom.csv" "$1.kicad_sch"; then
  echo; echo "Failed to export BOM!"
  exit 1
fi

echo "Filtering BOM and fixing rotations for JLCPCB..."
./fix-bom-and-pos.py \
    "$1-fab/$1.bom.csv" "$1-fab/$1.bom.csv" \
    "$1-fab/$1-front.pos.csv" "$1-fab/$1-front.pos.csv"

echo "Zipping..."
zip -r9 "$1-fab.zip" "$1-fab/"
