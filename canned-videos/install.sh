#!/bin/bash

SOURCE_ROOT="https://storage.googleapis.com/sega-kinetoscope/canned-videos"

FILES=(
  BOHEMIAN_RHAPSODY.segavideo
  DEVELOPERS_DEVELOPERS.segavideo
  GANGNAM_STYLE.segavideo
  NEVER_GONNA_GIVE_YOU_UP.segavideo
  SHIA_LABEOUF.segavideo
  ZOEY_ANN_THE_BOXER.segavideo
)

DESTINATION=~/.local/share/Kinetoscope-Emulation/
mkdir -p $DESTINATION

for FILE in "${FILES[@]}"; do
  if [[ ! -e "$DESTINATION/$FILE" ]]; then
    echo "Downloading $FILE..."
    curl --location "$SOURCE_ROOT/$FILE" -o "$DESTINATION/$FILE"
    echo ""
  fi
done
