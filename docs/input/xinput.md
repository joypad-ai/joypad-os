# XInput Input Interface

Reads Xbox controllers that use the XInput protocol rather than standard USB HID. Xbox 360 (wired), Xbox One, and Xbox Series X|S controllers all use XInput when connected via USB. This is a separate input path from [USB HID](usb-hid.md).

## Protocol

- **Bus**: USB 2.0 via TinyUSB host stack with `tusb_xinput` library
- **Method**: XInput-specific endpoint and report format (not HID)
- **Polling**: USB interrupt transfers, typically 4ms (250Hz)
- **Location**: `src/usb/usbh/xinput/`

XInput controllers do not expose standard HID descriptors. The `tusb_xinput` library (`src/lib/tusb_xinput`) adds XInput class support to TinyUSB, handling enumeration and report parsing for these devices.

## Supported Controllers

| Controller | Notes |
|------------|-------|
| Xbox 360 (wired) | Full support with rumble |
| Xbox 360 Wireless Adapter | Via USB wireless receiver |
| Xbox One (all revisions) | Full support with rumble |
| Xbox Series X\|S | Full support with rumble |
| Xbox Adaptive Controller | Full support |
| Xbox 360 Chatpad | Keyboard overlay accessory |

## Button Mapping

XInput reports are mapped to JP_BUTTON_* constants:

| Xbox Button | JP_BUTTON_* |
|-------------|-------------|
| A | B1 |
| B | B2 |
| X | B3 |
| Y | B4 |
| LB | L1 |
| RB | R1 |
| LT (digital) | L2 |
| RT (digital) | R2 |
| Back/View | S1 |
| Start/Menu | S2 |
| LS (click) | L3 |
| RS (click) | R3 |
| D-pad Up | DU |
| D-pad Down | DD |
| D-pad Left | DL |
| D-pad Right | DR |
| Guide/Xbox | A1 |

## Analog Axes

| Axis | Source | Normalization |
|------|--------|---------------|
| ANALOG_LX | Left stick X | Scaled from signed 16-bit to 0-255 |
| ANALOG_LY | Left stick Y | Scaled from signed 16-bit to 0-255 |
| ANALOG_RX | Right stick X | Scaled from signed 16-bit to 0-255 |
| ANALOG_RY | Right stick Y | Scaled from signed 16-bit to 0-255 |
| ANALOG_L2 | Left trigger | Scaled from 0-255 (already 8-bit) |
| ANALOG_R2 | Right trigger | Scaled from 0-255 (already 8-bit) |

## Chatpad Support

The Xbox 360 Chatpad keyboard accessory is supported. Chatpad key data is stored in the `input_event_t.chatpad[]` array (3 bytes: modifier + 2 keys) with `has_chatpad` set to true.

## Connection Detection

XInput device connect and disconnect are handled through TinyUSB enumeration callbacks, similar to HID. The `tusb_xinput` library detects XInput devices by their USB interface class during enumeration.

## Feedback

- **Rumble**: Dual motor rumble (left heavy, right light) via XInput output reports
- **Player LED**: Xbox 360 ring-of-light player indicator

## Configuration

- **Device address range**: 1-127 (shared with USB HID, assigned by TinyUSB)
- **Transport type**: `INPUT_TRANSPORT_USB`

## Apps Using This Input

XInput is available alongside USB HID in all USB-input apps:

- [usb2gc](../apps/usb2gc.md), [usb2pce](../apps/usb2pce.md), [usb2dc](../apps/usb2dc.md), [usb2nuon](../apps/usb2nuon.md), [usb23do](../apps/usb23do.md), [usb2loopy](../apps/usb2loopy.md)
- [usb2usb](../apps/usb2usb.md)
- [usb2uart](../apps/usb2uart.md)
