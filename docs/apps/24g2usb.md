# 24g2usb

8BitDo SF30 2.4G wireless receiver to USB HID gamepad.

## Overview

Drives an nRF24L01+ radio over SPI, impersonating the 8BitDo SF30 2.4G dongle closely enough that a SF30 2.4G controller pairs and links to it directly. The linked controller is presented to the host as a USB HID gamepad -- no 8BitDo dongle required. The receiver supports exactly one controller at a time; see "Single controller only" below for why.

## Input

[24G Input](../input/24g.md) -- interrupt-driven nRF24L01+ receiver decoding the SF30 2.4G wire protocol (64-channel frequency hopping, 13-byte frames). See [24G Protocol](../protocols/24g.md) for the full wire format.

## Output

[USB Device Output](../output/usb-device.md) -- USB HID gamepad with multiple emulation modes.

## Wiring

The nRF24L01+ module connects to `spi0`. CE and CSN are ordinary GPIOs driven by
the driver, not SPI peripheral functions.

| nRF24L01+ | Pico / Pico 2 (W) | Notes |
|-----------|-------------------|-------|
| VCC | 3V3 | Not 5V. Add a decoupling cap (10uF) across VCC/GND at the module -- these radios are notoriously sensitive to supply noise. |
| GND | GND | |
| CE | GPIO 4 | Plain GPIO |
| CSN | GPIO 5 | Plain GPIO (chip select, driven manually) |
| SCK | GPIO 6 | `spi0` SCK |
| MOSI | GPIO 7 | `spi0` TX |
| MISO | GPIO 0 | `spi0` RX |
| IRQ | GPIO 8 | Required -- the receiver is interrupt-driven, not polled |

GPIO 4 and GPIO 7 are not interchangeable: on RP2040/RP2350, GPIO 7 is the fixed
`spi0` TX function and GPIO 4 is `spi0` RX, so MOSI must be on 7 and CE on 4.

## Core Configuration

| Setting | Value |
|---------|-------|
| Routing mode | SIMPLE (1:1) |
| Player slots | 1 (fixed) |
| SCK pin | GPIO 6 (spi0) |
| MOSI pin | GPIO 7 (spi0) |
| MISO pin | GPIO 0 (spi0) |
| CSN pin | GPIO 5 |
| CE pin | GPIO 4 |
| IRQ pin | GPIO 8 |

Pins are clear of GP23/24/25/29, which the CYW43 module claims on `_w` boards.

## Key Features

- **No dongle required** -- a SF30 2.4G controller pairs and links directly, cold-acquiring in under a second.
- **Interrupt-driven radio** -- the receiver runs off the nRF24's IRQ line and a hardware alarm, not the main polling loop, so flash writes and other core-0 work can't stall a dwell and drop a packet.
- **Pairing gesture** -- hold the BOOTSEL button ~1.5s to begin pairing. See [24G Input](../input/24g.md#pairing) for the full flow.
- **LED indicator** -- solid while a controller is linked (`leds_set_connected_devices()`), fast ~100ms blink while a pairing rendezvous is in progress (`leds_set_pairing()`), slow ~500ms blink otherwise (idle, waiting for a controller).
- **USB output modes** -- SInput, XInput, PS3, PS4, Switch, Keyboard/Mouse (BOOTSEL double-click to cycle, triple-click to reset to HID).

## Single controller only

The receiver supports exactly one paired controller, matched by `MAX_PLAYER_SLOTS 1` and `USB_OUTPUT_PORTS 1`. USB output only ever surfaces player index 0 (`sinput_mode.c`, `hid_mode.c` and `xinput_mode.c` all `(void)player_index` and write to the single `ITF_NUM_HID_GAMEPAD` interface -- only `gc_adapter_mode.c` routes `player_index` to distinct USB interfaces, same limitation `bt2usb` carries), and two controllers hopping the same 64-channel table at independent phase could starve each other's dwell indefinitely if they powered on close in phase. Rather than ship a scheduler for a case USB couldn't deliver anyway, this app tracks a single controller. Pairing re-offers the same address and replaces whichever controller was previously paired to it.

## Supported Boards

| Board | Build Command |
|-------|---------------|
| Pico 2 W | `make 24g2usb_pico2_w` |
| Pico W | `make 24g2usb_pico_w` |
| Pico | `make 24g2usb_pico` |
| Pico 2 | `make 24g2usb_pico2` |

No CYW43 or BTstack needed -- these boards are used only for their onboard LED / form factor.

## Build and Flash

```bash
make 24g2usb_pico2_w
make flash-24g2usb_pico2_w
```
