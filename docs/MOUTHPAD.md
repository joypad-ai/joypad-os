# Augmental MouthPad Support

Joypad OS can bridge an Augmental **MouthPad** (a BLE mouth-controlled input
device) to USB, giving MouthPad users the full Joypad OS feature set — and
keeping the Augmental desktop utility working as if the MouthPad were connected
directly.

One USB composite device does everything at once:

- **Mouse + keyboard** to the host (cursor, clicks, typing, media keys).
- **Gamepad** to Steam/SDL/consoles — the MouthPad routes through the Joypad OS
  pipeline, so it's remappable via profiles and combinable with other
  controllers.
- **NUS relay over CDC** — the MouthPad's Nordic UART Service stream is bridged
  to the Augmental utility over a USB serial port.

## Boards

| Board | Build |
|-------|-------|
| April Brother nRF52840 dongle (shipped to MouthPad users) | `make mouthpad_aprbrother_nrf52840` |
| Raspberry Pi Pico W | `make mouthpad_pico_w` |
| Raspberry Pi Pico 2 W | `make mouthpad_pico2_w` |

Flash (nRF): `make flash-mouthpad_aprbrother_nrf52840` (double-tap reset →
`NRF52BOOT` drive). Pico W/2 W: drag the `releases/*.uf2` to `RPI-RP2`, or
`picotool load -x`.

> The `mouthpad_ble` BLE driver is registered in the shared BTHID registry, so
> any BT app (e.g. `bt2usb`) also recognises the MouthPad as a routed mouse. The
> dedicated `mouthpad` app additionally runs the NUS↔CDC relay.

## How it works

```
MouthPad (BLE)
  ├── HID-over-GATT (mouse/kbd/consumer)  ──► mouthpad_ble driver ──► router/profiles ──► SInput composite
  │                                                                                        (gamepad + mouse + keyboard)
  └── Nordic UART Service (6e40…)          ──► btstack NUS client ──► mp_bridge ──► USB CDC ──► Augmental utility
                                                  ▲                                   │
                                                  └─────────── NUS RX write ◄─────────┘  (host → device)
```

- **HID path**: `src/bt/bthid/devices/vendors/augmental/mouthpad_ble.c` parses
  the MouthPad's report map (R1 mouse buttons+wheel, R2 12-bit X/Y, R3 consumer,
  R4 keyboard), merges the split mouse reports, and submits `input_event_t`s.
  The SInput mouse interface emits **16-bit** X/Y to preserve the MouthPad's
  full 12-bit precision.
- **NUS relay**: `src/bt/mouthpad/mp_relay.c` implements the utility's wire
  contract (PacketFramer: `AA 55` + BE length + CRC-16/CCITT-FALSE; payload =
  `MouthpadRelay` `pass_through` protobufs). `mp_bridge.c` glues it to USB CDC
  (SPSC ring) and the BTstack NUS client (`btstack_host_mouthpad_nus_*`).

## Desktop utility

The Joypad OS bridge enumerates with the **SInput VID/PID `0x2E8A:0x10C6`** (it
does not impersonate Augmental's `0x1915:0xEEEE`). The Augmental utility was
updated to recognise this pair — see
`mouthpad-utility/Shared/Controllers/USBPeripheralsManager.swift`
(`supportedIDs`). With that build, the utility discovers the Joypad OS bridge
over USB and round-trips NUS passthrough (calibration, sensor streaming, config)
exactly as with the dedicated dongle.

> Minimal-passthrough scope: bidirectional `PassThrough` is implemented. The
> relay's `DeviceInfo` / `BleConnectionStatus` (battery/RSSI) messages are not
> yet implemented, so the utility's status chrome may be degraded until they are
> added.
