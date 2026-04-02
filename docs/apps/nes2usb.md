# nes2usb

NES controller to USB HID gamepad.

## Overview

Reads a native NES controller via PIO-based shift register protocol and outputs as a USB HID gamepad. 60Hz polling with fractional timing correction for accurate cadence. Automatic connect/disconnect detection with 500ms debounce and stuck-button prevention.

## Input

[NES Input](../input/nes.md) -- PIO shift register protocol at 1MHz instruction clock. CLK (GPIO 5), LATCH (GPIO 6), DATA (GPIO 8).

## Output

[USB Device Output](../output/usb-device.md) -- USB HID gamepad with multiple emulation modes.

## Core Configuration

| Setting | Value |
|---------|-------|
| Routing mode | SIMPLE (1:1) |
| Player slots | 1 (fixed) |
| Polling rate | 60Hz (fractional correction for accuracy) |
| Profile system | None |

## Key Features

- **Auto-detection** -- Controller connected/disconnected detected via DATA line state.
- **USB output modes** -- SInput, XInput, PS3, PS4, Switch, Keyboard/Mouse. Double-click button to cycle, triple-click to reset.
- **Web config** -- [config.joypad.ai](https://config.joypad.ai).

## Supported Boards

| Board | Build Command |
|-------|---------------|
| KB2040 | `make nes2usb_kb2040` |
| Pico W | `make nes2usb_pico_w` |

## Build and Flash

```bash
make nes2usb_kb2040
make flash-nes2usb_kb2040
```

## Troubleshooting

**Controller not detected:**
- Check NES port wiring, especially GND and VCC.
- Verify CLK, LATCH, and DATA pin assignments match your board.
- Ensure the NES controller is fully seated in the connector.

**No response from buttons:**
- Confirm the adapter is powered (NeoPixel LED should be lit).
- Try a different NES controller to rule out a faulty controller.
- Check that the DATA line has continuity from the controller port to GPIO 8.

**Wrong buttons or garbled input:**
- Verify CLK and LATCH are not swapped.
- Check for cold solder joints on signal lines.
- Use [config.joypad.ai](https://config.joypad.ai) input monitor to view raw input state.

**USB not recognized by host:**
- Double-click the board button to cycle USB output mode.
- Triple-click to reset to SInput (default HID mode).
- Try a different USB cable or port.
