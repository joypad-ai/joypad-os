# USB HID Input Interface

Reads USB HID controllers, keyboards, and mice via the TinyUSB host stack. This is the most common input path -- any standard USB gamepad plugged into the adapter is handled here. Vendor-specific drivers provide enhanced support for popular controllers, while a generic HID parser handles everything else.

## Protocol

- **Bus**: USB 2.0 Full-Speed (12 Mbps) via TinyUSB host stack
- **Method**: HID report parsing (generic) or vendor-specific report decoding
- **Polling**: USB interrupt transfers at the device's requested interval (typically 1-8ms)
- **Location**: `src/usb/usbh/hid/`

USB HID input runs on Core 0 alongside the main loop. TinyUSB's host task (`tuh_task()`) drives enumeration, polling, and report callbacks.

## Device Registry

The HID registry (`src/usb/usbh/hid/hid_registry.h`) matches incoming devices by VID/PID to vendor-specific drivers. When a device connects, the registry walks its table of `DeviceInterface` entries:

1. Each registered driver's `is_device(vid, pid)` is called
2. If a match is found, that driver handles init, report processing, and disconnect
3. If no match, the generic SInput or DInput parser handles the device

Registered controller types:

| Type | Driver | Vendor Directory |
|------|--------|-----------------|
| DualShock 3 | `sony_ds3` | `vendors/sony/` |
| DualShock 4 | `sony_ds4` | `vendors/sony/` |
| DualSense / DualSense Edge | `sony_ds5` | `vendors/sony/` |
| PlayStation Classic | `sony_psc` | `vendors/sony/` |
| Switch Pro | `switch_pro` | `vendors/nintendo/` |
| Switch 2 Pro | `switch2_pro` | `vendors/nintendo/` |
| GameCube Adapter | `gamecube_adapter` | `vendors/nintendo/` |
| 8BitDo BT Adapter | `8bitdo_bta` | `vendors/8bitdo/` |
| 8BitDo M30 | `8bitdo_m30` | `vendors/8bitdo/` |
| 8BitDo PCEngine | `8bitdo_pce` | `vendors/8bitdo/` |
| 8BitDo Neo Geo | `8bitdo_neo` | `vendors/8bitdo/` |
| HORI Pokken | `hori_pokken` | `vendors/hori/` |
| HORI Horipad | `hori_horipad` | `vendors/hori/` |
| Logitech Wingman | `logitech_wingman` | `vendors/logitech/` |
| Sega Astrocity | `sega_astrocity` | `vendors/sega/` |
| Google Stadia | `google_stadia` | `vendors/google/` |
| Raphnet PCE | `raphnet_pce` | `vendors/raphnet/` |
| Sidewinder DualStrike | `ms_sidewinder_dualstrike` | `vendors/microsoft/` |
| Sidewinder Commander | `ms_sidewinder_commander` | `vendors/microsoft/` |
| DragonRise | `dragonrise` | `vendors/dragonrise/` |
| Generic SInput | `sinput` | (built-in) |
| Generic DInput | `dinput` | (built-in) |
| Keyboard | `keyboard` | (built-in) |
| Mouse | `mouse` | (built-in) |

## Supported Controllers

See the [controllers list](../hardware/controllers.md) for the full compatibility table. Key categories:

- **Xbox**: Original, 360, One, Series (via [XInput](xinput.md), not HID)
- **PlayStation**: PS Classic, DS3, DS4, DualSense -- full rumble, touchpad, motion
- **Nintendo**: Switch Pro, Switch 2 Pro, Joy-Con, GC Adapter (4 ports), NSO controllers
- **8BitDo**: PCEngine 2.4g, M30 2.4g/BT, Neo Geo, wireless adapters
- **Arcade/Retro**: Sega Astrocity, HORI Pokken/Horipad, Logitech Wingman, Raphnet adapters
- **Generic**: Any USB HID gamepad, keyboard, or mouse

## Button Mapping

Vendor drivers normalize all buttons to JP_BUTTON_* constants. The generic DInput/SInput parsers use HID usage tables to map standard gamepad buttons automatically.

## Analog Axes

All axes are normalized to 0-255 with 128 as center per the `analog_axis_index_t` enum:

| Index | Axis | Range |
|-------|------|-------|
| ANALOG_LX (0) | Left stick X | 0=left, 128=center, 255=right |
| ANALOG_LY (1) | Left stick Y | 0=up, 128=center, 255=down |
| ANALOG_RX (2) | Right stick X | 0=left, 128=center, 255=right |
| ANALOG_RY (3) | Right stick Y | 0=up, 128=center, 255=down |
| ANALOG_L2 (4) | Left trigger | 0=released, 255=fully pressed |
| ANALOG_R2 (5) | Right trigger | 0=released, 255=fully pressed |
| ANALOG_RZ (6) | Twist/spinner | 0=released, 255=fully pressed |

## Connection Detection

TinyUSB handles USB enumeration and disconnect events. When a device is plugged in, `tuh_hid_mount_cb()` fires and the registry identifies the device. On unplug, `tuh_hid_umount_cb()` fires and cleared input is submitted to prevent stuck buttons.

## Feedback

Vendor-specific drivers support:

- **Rumble**: DS3, DS4, DualSense, Switch Pro (via output reports)
- **LED**: DS4 lightbar color, DualSense lightbar, player LEDs
- **Motion**: DS3 SIXAXIS, DS4 gyro/accel, DualSense gyro/accel
- **Touchpad**: DS4 and DualSense 2-finger capacitive touch
- **Pressure**: DS3 pressure-sensitive face buttons and triggers

## Configuration

- **Device address range**: 1-127 (assigned by TinyUSB)
- **USB hubs**: Supported -- up to 8 simultaneous devices through standard USB 2.0 hubs
- **Transport type**: `INPUT_TRANSPORT_USB`

## Apps Using This Input

USB HID input is available in all apps that accept USB controllers:

- [usb2gc](../apps/usb2gc.md), [usb2pce](../apps/usb2pce.md), [usb2dc](../apps/usb2dc.md), [usb2nuon](../apps/usb2nuon.md), [usb23do](../apps/usb23do.md), [usb2loopy](../apps/usb2loopy.md)
- [usb2usb](../apps/usb2usb.md)
- [usb2uart](../apps/usb2uart.md)
