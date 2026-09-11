#!/usr/bin/env python3
# Combine the dual-RP2040 remapper images into one drag-drop UF2.
#
#   combine_uf2.py <A.uf2> <flash_b_side.uf2> <B.bin> <out.uf2>
#
# Layout written to the A (device, USB-C) RP2040's flash + RAM:
#   - A's firmware        (flash, from A.uf2, at 0x10000000)
#   - B's image + header  (flash, from B.bin, at 0x10000000 + B_IMAGE_OFFSET)
#   - flash_b_side relay  (RAM, from flash_b_side.uf2, no_flash)
#
# The RP2040 bootloader writes the flash blocks, then loads + runs the RAM
# relay. The relay reads B's image straight from A's XIP flash (in chunks) and
# writes it to B over SWD — so B's image never has to fit in the relay's RAM.
# This lifts the old limit where B (embedded in the relay's .rodata) had to fit
# in 264 KB of RAM; B can now be as large as the flash budget allows (e.g. with
# BTstack for USB Bluetooth dongle support).
#
# The flash blocks' "total blocks" field is patched larger than the real count
# so the bootloader does not consider the download complete after the flash
# blocks (which would boot A before B is flashed); it stays in BOOTSEL, loads
# the RAM relay, and runs it. (Trick from jfedor2/hid-remapper, MIT.)
import struct
import sys

XIP_BASE = 0x10000000
# B's image is placed this far into A's flash. Must clear A's firmware (asserted
# below) and, with B's image, fit within the board's flash (pico = 2 MB). A is
# ~210 KB, so 256 KB leaves headroom; B (with BTstack, ~450 KB) then ends well
# under 1 MB. The region only has to survive from bootloader-write until the
# relay reads it this same boot, so A's later runtime flash writes don't matter.
B_IMAGE_OFFSET = 0x40000
B_IMAGE_MAGIC = 0x42494D47  # "BIMG" (LE) — relay checks this before flashing.

UF2_MAGIC0 = 0x0A324655
UF2_MAGIC1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_ID = 0x00002000
RP2040_FAMILY_ID = 0xE48BFF56
UF2_PAYLOAD = 256


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


def max_flash_addr(blocks):
    hi = 0
    for b in blocks:
        addr, = struct.unpack_from("<I", b, 12)
        psz, = struct.unpack_from("<I", b, 16)
        hi = max(hi, addr + psz)
    return hi


def make_uf2_block(addr, payload):
    # payload is <= UF2_PAYLOAD bytes; block numbers/count are filled in later.
    block = bytearray(512)
    struct.pack_into("<IIIIIIII", block, 0,
                     UF2_MAGIC0, UF2_MAGIC1, UF2_FLAG_FAMILY_ID, addr,
                     UF2_PAYLOAD, 0, 0, RP2040_FAMILY_ID)
    block[32:32 + len(payload)] = payload
    struct.pack_into("<I", block, 508, UF2_MAGIC_END)
    return block


def blocks_for_bytes(data, base_addr):
    blocks = []
    for off in range(0, len(data), UF2_PAYLOAD):
        chunk = data[off:off + UF2_PAYLOAD]
        blocks.append(make_uf2_block(base_addr + off, chunk))
    return blocks


def main():
    if len(sys.argv) != 5:
        sys.stderr.write("usage: combine_uf2.py <A.uf2> <relay.uf2> <B.bin> <out.uf2>\n")
        sys.exit(2)

    a_uf2, relay_uf2, b_bin, out_uf2 = sys.argv[1:5]

    a_blocks = read_uf2_blocks(a_uf2)
    ram_blocks = read_uf2_blocks(relay_uf2)

    with open(b_bin, "rb") as f:
        b_image = f.read()

    # Pad B's image up to a 4 KB flash-sector boundary with 0xFF (erased state).
    # The relay streams B in 64 KB chunks and its final flash_range_program call
    # gets a length = len % 64K; the RP2040 ROM flash program requires a multiple
    # of 256, and erase works in 4K sectors. An unpadded image (e.g. 449152 bytes
    # -> tail 55936, not a 256-multiple) makes that ROM call fault on B, hanging
    # the relay so A never reboots and B is left unflashed. Pad to 4K to keep
    # every chunk 256- and 4K-aligned. The trailing 0xFF bytes are unused by B.
    SECTOR = 4096
    if len(b_image) % SECTOR:
        b_image += b"\xff" * (SECTOR - (len(b_image) % SECTOR))

    # Guard: A's firmware must not reach into B's image region.
    a_hi = max_flash_addr(a_blocks)
    if a_hi > XIP_BASE + B_IMAGE_OFFSET:
        sys.stderr.write(
            "error: A firmware ends at 0x%08X, past B image offset 0x%08X — "
            "raise B_IMAGE_OFFSET in combine_uf2.py\n" % (a_hi, XIP_BASE + B_IMAGE_OFFSET))
        sys.exit(1)

    # B payload = [magic u32][length u32][raw B image].
    b_payload = struct.pack("<II", B_IMAGE_MAGIC, len(b_image)) + b_image
    b_blocks = blocks_for_bytes(b_payload, XIP_BASE + B_IMAGE_OFFSET)

    flash_blocks = a_blocks + b_blocks
    patched_total = max(len(flash_blocks), len(ram_blocks)) + 1

    # Renumber all flash blocks into one consistent sequence, with numBlocks set
    # to the (deliberately too-large) patched total so the bootloader stays put.
    with open(out_uf2, "wb") as f:
        for i, block in enumerate(flash_blocks):
            struct.pack_into("<I", block, 20, i)                 # blockNo
            struct.pack_into("<I", block, 24, patched_total)     # numBlocks
            f.write(block)
        for block in ram_blocks:
            f.write(block)

    print("combine_uf2: A=%d blocks, B image=%d bytes (%d blocks) @ 0x%08X, relay=%d RAM blocks"
          % (len(a_blocks), len(b_image), len(b_blocks), XIP_BASE + B_IMAGE_OFFSET, len(ram_blocks)))


if __name__ == "__main__":
    main()
