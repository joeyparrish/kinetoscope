#!/bin/bash

# Kinetoscope: A Sega Genesis Video Player
#
# Copyright (c) 2024 Joey Parrish
#
# See MIT License in LICENSE.txt

# Universal build script, symlinked into each project folder.
# If you run this from the project root, you will get an error message with
# instructions.

set -e

# The folder this script is in.
this_dir="$(dirname "$0")"
# The repo root.
repo_root="$(git rev-parse --show-toplevel)"
# this_dir, relative to repo_root.
relative_dir="$(realpath -s --relative-to="$repo_root" "$this_dir")"

if [[ ! -L "$0" ]]; then
  echo "This universal build script is meant to run as a symlink from a project folder."
  echo "Please run the build for a specific project, such as:"
  echo ""
  echo "  ./stream-with-special-hardware/build.sh"
  echo ""
  echo "See also the README.md files at the root and in each project folder."
  exit 1
fi

if [[ ! -d "$this_dir/src" ]]; then
  echo "Sources not found in this folder!"
  exit 1
fi

# Run docker.
# Delete the container afterward (--rm).
# Mount the repo root into /src (-v ...) so that symlinks work within the repo.
# Run as the local user (-u) so that the container doesn't output as root.
# Set the working directory to within /src (-w) to build a specific sample.
# Pull the SGDK compiler docker image from GitHub.
docker run \
  --rm \
  -v "$repo_root":/src \
  -u $(id -u):$(id -g) \
  -w "/src/$relative_dir" \
  ghcr.io/stephane-d/sgdk:latest

# SGDK's compiler makes the output executable, but that's not appropriate.
chmod 644 $this_dir/out/rom.bin

# Show the size of the output.
ls -sh $this_dir/out/rom.bin
