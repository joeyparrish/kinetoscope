#!/bin/bash

# Kinetoscope: A Sega Genesis Video Player
#
# Copyright (c) 2024 Joey Parrish
#
# See MIT License in LICENSE.txt

# Run FreeRouting to route a PCB.
# Export the DSN file interactively (no support in kicad-cli yet).
# Pass the name of a project without extension, e.g. "cart"

rm -f "$1.ses"

# Remove ground planes from the DSN file.  FreeRouting will freak out on those.
perl -i -0pe 's/\(plane GND \(polygon.*?\)\)//sg' "$1.dsn"

if ! time java -jar \
    /opt/freerouting/freerouting-executable.jar -- \
    -de "$1".dsn -do "$1".ses -da \
    -dct 3 -mp 100 -oit 0.5 -mt 1; then
  echo; echo "Failed to route PCB!"
  exit 1
fi

echo "Routing complete.  Import $1.ses into the PCB editor and regen fills."
