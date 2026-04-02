# usb2nuon

USB/BT controllers to Nuon DVD player.

## Overview

Connects USB and Bluetooth controllers to a Nuon-enhanced DVD player via the Polyface serial protocol. Supports spinner emulation for Tempest 3000 using a USB mouse, and In-Game Reset (IGR) to return to the DVD menu or power off without getting up.

## Input

- [USB HID](../input/usb-hid.md), [XInput](../input/xinput.md), [Bluetooth](../input/bluetooth.md) controllers
- USB mouse (spinner emulation for Tempest 3000)

## Output

[Nuon Output](../output/nuon.md) -- Polyface PIO protocol.

## Core Configuration

| Setting | Value |
|---------|-------|
| Routing mode | SIMPLE (1:1) |
| Player slots | 1 (shift on disconnect) |
| Max USB devices | 1 |
| Profile system | Yes |
| Spinner support | Right stick / mouse X-axis to spinner |

## Key Features

- **Spinner emulation** -- USB mouse X-axis maps to Nuon spinner rotation. Left click maps to fire. Optimized for Tempest 3000.
- **In-Game Reset (IGR)** -- Hold L1 + R1 + Start + Select:
  - Tap (release before 2s): sends Stop (returns to DVD menu)
  - Hold 2+ seconds: sends Power (powers off the player)
- **Profiles** -- Hold Select 2s, then D-Pad Up/Down to cycle.

## Supported Boards

| Board | Build Command |
|-------|---------------|
| KB2040 | `make usb2nuon_kb2040` |

## Build and Flash

```bash
make usb2nuon_kb2040
make flash-usb2nuon_kb2040
```

## Compatible Hardware

### Nuon DVD Players
- Samsung DVD-N501
- Samsung DVD-N504 / N505
- Toshiba SD-2300
- Motorola Streamaster 5000
- RCA DRC300N / DRC480N

## Compatible Games

### Standard Controller
- Iron Soldier 3
- Ballistic
- Space Invaders XL
- Merlin Racing
- Freefall 3050 A.D.
- The Next Tetris

### Spinner (Tempest 3000)
- Tempest 3000 -- premium spinner experience with USB mouse
- VLM-2 (audio visualizer)

## Troubleshooting

**Controller not detected:**
- Check Nuon port connections, especially power and ground.
- Verify data pins match the protocol.

**IGR not working:**
- Hold all four buttons (L1 + R1 + Start + Select) simultaneously.
- Tap for Stop, hold 2+ seconds for Power.
- Some Nuon players may not respond to all functions.

**Spinner too sensitive or too slow:**
- Adjust mouse DPI settings on the mouse itself.
- Modify `NUON_SPINNER_SCALE` in firmware for fine tuning.
- Use an optical mouse for best results.

**Tempest 3000 spinner issues:**
- Verify the USB mouse is detected by the adapter.
- Try a lower DPI setting.
- Check mouse polling rate.

**Buttons not responding:**
- Verify button mapping matches game expectations.
- Test with a known-good USB controller.
