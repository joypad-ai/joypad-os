# Bluetooth Input Interface

Reads wireless Bluetooth controllers via BTstack. Supports both Bluetooth Classic (HID) and Bluetooth Low Energy (HOGP/BLE HID). The vendor driver structure mirrors [USB HID](usb-hid.md) -- most controllers that work over USB also work over Bluetooth with the same button mappings.

## Protocol

- **Stack**: BTstack (Classic HID + BLE HOGP)
- **Method**: HID report parsing with per-vendor drivers in `src/bt/bthid/devices/`
- **Polling**: Controller-driven (reports arrive when input changes or at the controller's internal rate)
- **Location**: `src/bt/` (host integration at `src/bt/btstack/btstack_host.c`)

## Platform Support

| Platform | Classic BT | BLE | Radio |
|----------|-----------|-----|-------|
| Pico W / Pico 2 W | Yes | Yes | CYW43 built-in |
| RP2040 + USB dongle | Yes | Yes | External USB BT dongle |
| ESP32-S3 | No | Yes | Built-in (BLE only) |
| nRF52840 | No | Yes | Built-in (BLE only) |

Classic BT controllers (DualShock 3/4, DualSense, Switch Pro, Wii U Pro) require the Pico W or a USB Bluetooth dongle. BLE-only platforms support Xbox One/Series (BLE mode), 8BitDo (BLE mode), Switch 2 Pro (BLE), and generic BLE HID gamepads.

## Supported Controllers

### Classic BT + BLE (Pico W / USB Dongle)

| Controller | Status |
|------------|--------|
| DualShock 3 (PS3) | Supported (with rumble) |
| DualShock 4 (PS4) | Supported (with rumble, touchpad) |
| DualSense (PS5) | Supported (with rumble) |
| Switch Pro Controller | Supported (with rumble) |
| Switch 2 Pro Controller | Supported |
| Wii U Pro Controller | Supported |
| NSO GameCube | Supported |
| Xbox One / Series (BT mode) | Supported |
| Google Stadia | Supported |
| Generic BT HID | Basic support |

### BLE Only (ESP32-S3 / nRF52840)

| Controller | Status |
|------------|--------|
| Xbox One / Series (BLE mode) | Supported |
| 8BitDo controllers (BLE mode) | Supported |
| Switch 2 Pro (BLE) | Supported |
| Generic BLE HID gamepads | Supported |

## Bluetooth Dongles

Only dongles with firmware in ROM work on embedded. Most BT 5.0+ dongles use Realtek chips that require host-side firmware loading and will not work.

**Supported chipsets**: Broadcom (BCM20702A0), CSR/Cambridge Silicon Radio (CSR8510 A10)

**Not supported**: Realtek (RTL8761B, etc.) -- almost all BT 5.0+ dongles

See the [controllers list](../hardware/controllers.md) for tested dongle models.

## Pairing and Reconnection

1. The adapter enters scan/pairing mode automatically on boot
2. Put the controller into pairing mode
3. BTstack discovers and pairs the device
4. On subsequent boots, the adapter scans for new devices while also attempting to reconnect to the last bonded device
5. First reconnect attempt after 3 seconds, then every 20 seconds
6. Bond data is persisted to flash (NVS on ESP32/nRF, flash bank on RP2040)

## Button Mapping

Bluetooth vendor drivers produce the same JP_BUTTON_* mappings as their USB counterparts. The driver structure under `src/bt/bthid/devices/` mirrors `src/usb/usbh/hid/devices/`.

## Connection Detection

BTstack fires connection and disconnection callbacks. On disconnect, cleared input is submitted to prevent stuck buttons. Connection state is tracked per `conn_index`.

## Feedback

- **Rumble**: DS3, DS4, DualSense, Switch Pro (via BT output reports)
- **Player LED**: DS4 lightbar, player number indicators

## Configuration

- **Device address**: Uses BTstack connection index (not a fixed range)
- **Transport type**: `INPUT_TRANSPORT_BT_CLASSIC` or `INPUT_TRANSPORT_BT_BLE`
- **Classic BT guards**: APIs like `gap_inquiry_*`, `gap_set_class_of_device()`, and `gap_discoverable_control()` are Classic-only and must be guarded with `#ifndef BTSTACK_USE_ESP32` / `#ifndef BTSTACK_USE_NRF`

## Apps Using This Input

Bluetooth input is available in apps that include BTstack:

- [usb2gc](../apps/usb2gc.md), [usb2pce](../apps/usb2pce.md), [usb2dc](../apps/usb2dc.md), [usb2nuon](../apps/usb2nuon.md), [usb23do](../apps/usb23do.md), [usb2loopy](../apps/usb2loopy.md) -- via USB BT dongle
- [bt2usb](../apps/bt2usb.md) -- Pico W, ESP32-S3, or nRF52840 built-in radio
- [bt2gc](../apps/bt2gc.md), [bt2n64](../apps/bt2n64.md), [bt2nuon](../apps/bt2nuon.md) -- Pico W / Pico 2 W
