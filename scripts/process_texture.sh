#!/bin/bash

if [ -z "$1" ]; then
  echo "Usage: $0 <filename_prefix>"
  exit 1
fi

PREFIX=$1

magick "${PREFIX}_roughness.png" "${PREFIX}_metallic.png" "${PREFIX}_height.png" -channel RGB -combine "${PREFIX}_orm.png"

texconv -f BC7_UNORM -y -m 0 -srgb:off "${PREFIX}_orm.png"
