#!/usr/bin/env python3
# Combines two UF2 files: first a normal flash binary, second a RAM-only binary.
# Patches the "total blocks" field of the flash blocks so it differs from the
# actual block counts, which makes the RP2040 bootloader write the first binary
# to flash, then load the second (RAM) binary and run it. Used to flash the
# host-side (B) RP2040 over SWD at install time from the device-side (A) image.
#
# From jfedor2/hid-remapper (combine_uf2.py), MIT.
import struct
import sys


def read_uf2_blocks(filename):
    blocks = []
    with open(filename, "rb") as f:
        while True:
            block = f.read(512)
            if not block:
                break
            if len(block) != 512:
                raise Exception("block size != 512")
            blocks.append(bytearray(block))
    return blocks


flash_blocks = read_uf2_blocks(sys.argv[1])
ram_blocks = read_uf2_blocks(sys.argv[2])

patched_total_blocks = max(len(flash_blocks), len(ram_blocks)) + 1

with open(sys.argv[3], "wb") as f:
    for block in flash_blocks:
        struct.pack_into("<I", block, 24, patched_total_blocks)
        f.write(block)
    for block in ram_blocks:
        f.write(block)
