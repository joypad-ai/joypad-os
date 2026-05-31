#!/bin/sh
# Build for the Waveshare RP2350B-Plus-W. The board header lives under this
# repo (not pico-sdk) -- point pico-sdk at it via PICO_BOARD_HEADER_DIRS.
cmake -G "Unix Makefiles" \
      -DPICO_BOARD=waveshare_rp2350b_plus_w \
      -DPICO_BOARD_HEADER_DIRS="$(pwd)/boards/headers" \
      -B build
