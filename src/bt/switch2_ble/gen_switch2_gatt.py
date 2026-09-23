#!/usr/bin/env python3
# gen_switch2_gatt.py - emit switch2_gatt_db.h (BTstack ATT DB for a Switch 2 Pro Controller)
# SPDX-License-Identifier: Apache-2.0
#
# BTstack's compile_gatt.py can't declare vendor 128-bit *descriptors*, and the Switch 2
# console addresses the controller by fixed handles, so the attribute table has to be
# reproduced exactly. This script encodes the table the same way att_db_util.c does and
# asserts every handle against the real Pro Controller 2 map (ndeadly/
# switch2_controller_research bluetooth_interface.md, fw >= 2.0 with headset attributes).
# The output is a const array in flash (zero RAM, unlike att_db_util at runtime).
#
# Usage: python3 gen_switch2_gatt.py > switch2_gatt_db.h

import sys

# BTstack ATT flags (bluetooth.h / att_db.h)
READ, WNR, WRITE, NOTIFY = 0x02, 0x04, 0x08, 0x10
DYNAMIC, UUID128 = 0x100, 0x200

db = bytearray([1])  # ATT_DB_VERSION
handle = 1
defines = []


def uuid128(s):
    b = bytes.fromhex(s.replace('-', ''))
    assert len(b) == 16
    return b[::-1]  # little-endian, as att_db_util's reverse_128


def attr16(uuid16, flags, value):
    global handle
    h = handle
    db.extend((8 + len(value)).to_bytes(2, 'little'))
    db.extend(flags.to_bytes(2, 'little'))
    db.extend(h.to_bytes(2, 'little'))
    db.extend(uuid16.to_bytes(2, 'little'))
    db.extend(value)
    handle += 1
    return h


def attr128(uuid, flags, value):
    global handle
    h = handle
    db.extend((22 + len(value)).to_bytes(2, 'little'))
    db.extend((flags | UUID128).to_bytes(2, 'little'))
    db.extend(h.to_bytes(2, 'little'))
    db.extend(uuid128(uuid))
    db.extend(value)
    handle += 1
    return h


def service(uuid):
    if isinstance(uuid, int):
        return attr16(0x2800, READ, uuid.to_bytes(2, 'little'))
    return attr16(0x2800, READ, uuid128(uuid))


def characteristic(uuid, props, value=None, name=None, expect=None):
    """Declaration + value (+ CCC for notify). value=None -> DYNAMIC (served by callback)."""
    if isinstance(uuid, int):
        decl = bytes([props]) + (handle + 1).to_bytes(2, 'little') + uuid.to_bytes(2, 'little')
    else:
        decl = bytes([props]) + (handle + 1).to_bytes(2, 'little') + uuid128(uuid)
    attr16(0x2803, READ, decl)
    flags = props & 0xff4e  # drop broadcast/notify/indicate/ext (att_db_util_encode_permissions)
    if value is None:
        flags |= DYNAMIC
        value = b''
    if isinstance(uuid, int):
        vh = attr16(uuid, flags, value)
    else:
        vh = attr128(uuid, flags, value)
    if expect is not None:
        assert vh == expect, f'{name}: value handle 0x{vh:04x} != expected 0x{expect:04x}'
    if name:
        defines.append((f'SW2_H_{name}', vh))
    if props & NOTIFY:
        # CCC exactly as att_db_util_add_client_characteristic_configuration
        ccc = attr16(0x2902, (flags & 0x1f391) | READ | WRITE | DYNAMIC, b'\x00\x00')
        if name:
            defines.append((f'SW2_H_{name}_CCC', ccc))
    return vh


def descriptor(uuid, name=None, expect=None):
    h = attr128(uuid, READ | WRITE | DYNAMIC, b'')
    if expect is not None:
        assert h == expect, f'{name}: descriptor handle 0x{h:04x} != 0x{expect:04x}'
    if name:
        defines.append((f'SW2_H_{name}', h))
    return h


RATE_DESC = '679d5510-5a24-4dee-9557-95df80486ecb'
RSP_DESC = 'b746df8c-f358-495b-9cd2-e3bbeda4f979'

# --- Service 1 (0x0001-0x0007). 281/283 values from the known-working zhantss emulator.
assert service('00c5af5d-1964-4e30-8f51-1956f96bd280') == 0x0001
characteristic('00c5af5d-1964-4e30-8f51-1956f96bd281', READ,
               bytes.fromhex('04000500010100'), 'SVC1_281', 0x0003)
characteristic('00c5af5d-1964-4e30-8f51-1956f96bd282', WRITE, None, 'SVC1_282', 0x0005)
characteristic('00c5af5d-1964-4e30-8f51-1956f96bd283', READ,
               bytes.fromhex('368074eebb3d8e13'), 'SVC1_283', 0x0007)

# --- Service 2 (0x0008-0x0032), Pro Controller 2 UUIDs
assert service('ab7de9be-89fe-49ad-828f-118f09df7fd0') == 0x0008
characteristic('ab7de9be-89fe-49ad-828f-118f09df7fd2', READ | NOTIFY, None, 'COMMON_INPUT', 0x000a)
descriptor(RATE_DESC, 'COMMON_INPUT_RATE', 0x000c)
characteristic('7492866c-ec3e-4619-8258-32755ffcc0f8', READ | NOTIFY, None, 'INPUT', 0x000e)
descriptor(RATE_DESC, 'INPUT_RATE', 0x0010)
characteristic('cc483f51-9258-427d-a939-630c31f72b05', WNR, None, 'VIBRATION', 0x0012)
characteristic('649d4ac9-8eb7-4e6c-af44-1ea54fe5f005', WNR, None, 'COMMAND', 0x0014)
characteristic('3dacbc7e-6955-40b5-8eaf-6f9809e8b379', WNR, None, 'VIB_COMMAND', 0x0016)
characteristic('4147423d-fdae-4df7-a4f7-d23e5df59f8d', WNR, None, 'FW_UPDATE', 0x0018)
characteristic('c765a961-d9d8-4d36-a20a-5315b111836a', NOTIFY, None, 'RESPONSE1', 0x001a)
descriptor(RSP_DESC, 'RESPONSE1_DESC', 0x001c)
characteristic('506d9f7d-4278-4e95-a549-326ba77657e0', NOTIFY, None, 'RESPONSE2', 0x001e)
descriptor(RSP_DESC, 'RESPONSE2_DESC', 0x0020)
characteristic('d3bd69d2-841c-4241-ab15-f86f406d2a80', NOTIFY, None, 'UNKNOWN_22', 0x0022)
descriptor(RSP_DESC, 'UNKNOWN_22_DESC', 0x0024)
characteristic('ab7de9be-89fe-49ad-828f-118f09df7fde', READ | NOTIFY, None, 'UNKNOWN_26', 0x0026)
descriptor(RATE_DESC, 'UNKNOWN_26_RATE', 0x0028)
characteristic('ab7de9be-89fe-49ad-828f-118f09df7fdf', WNR, None, 'UNKNOWN_2A', 0x002a)
# Headset-audio attributes of an updated (fw >= 2.0) Pro Controller 2. Without them the
# console treats the pad as factory firmware and diverges (espp, HW-verified).
characteristic('cc483f51-9258-427d-a939-630c31f72b06', WNR, None, 'AUDIO_OUT', 0x002c)
characteristic('7492866c-ec3e-4619-8258-32755ffcc0f9', READ | NOTIFY, None, 'AUDIO_IN', 0x002e)
descriptor(RATE_DESC, 'AUDIO_IN_RATE', 0x0030)
characteristic('3dacbc7e-6955-40b5-8eaf-6f9809e8b380', WNR, None, 'AUDIO_COMMAND', 0x0032)

# --- GAP / GATT last (a real controller has them after the vendor services; the
# console rejects a layout with them first). GATT service has no children, as on the pad.
assert service(0x1800) == 0x0033
characteristic(0x2A00, READ, b'Pro Controller', 'DEVICE_NAME', 0x0035)
characteristic(0x2A01, READ, (0x03C4).to_bytes(2, 'little'), 'APPEARANCE', 0x0037)
assert service(0x1801) == 0x0038

db.extend(b'\x00\x00')  # end tag

out = sys.stdout
out.write('// switch2_gatt_db.h - GENERATED by gen_switch2_gatt.py, do not edit\n')
out.write('// SPDX-License-Identifier: Apache-2.0\n')
out.write('//\n// BTstack ATT DB reproducing the Switch 2 Pro Controller (fw >= 2.0) handle map.\n\n')
out.write('#ifndef SWITCH2_GATT_DB_H\n#define SWITCH2_GATT_DB_H\n\n#include <stdint.h>\n\n')
for name, h in defines:
    out.write(f'#define {name:<28} 0x{h:04x}\n')
out.write(f'\nstatic const uint8_t switch2_gatt_db[{len(db)}] = {{\n')
for i in range(0, len(db), 16):
    out.write('    ' + ', '.join(f'0x{b:02x}' for b in db[i:i + 16]) + ',\n')
out.write('};\n\n#endif // SWITCH2_GATT_DB_H\n')
