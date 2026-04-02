# usb2pce

USB/BT controllers to PCEngine/TurboGrafx-16 console.

## Overview

Connects USB and Bluetooth controllers to a PCEngine or TurboGrafx-16 via the multitap protocol. Emulates up to 5 players simultaneously. Supports USB mouse as a PCEngine mouse for compatible games. Automatically handles 2-button, 3-button, and 6-button mode switching based on what the game requests.

## Input

- [USB HID](../input/usb-hid.md), [XInput](../input/xinput.md), [Bluetooth](../input/bluetooth.md) controllers
- USB mouse (mapped to PCEngine mouse protocol)

## Output

[PCEngine Output](../output/pcengine.md) -- PIO-based multitap emulation (plex.pio, clock.pio, select.pio).

## Core Configuration

| Setting | Value |
|---------|-------|
| Routing mode | SIMPLE (1:1 controller to multitap slot) |
| Player slots | 5 (shift on disconnect) |
| Max USB devices | 6 |
| Profile system | None (pass-through) |

## Key Features

- **5-player multitap** -- Controllers assigned in connection order. Works with Bomberman '93/'94, Dungeon Explorer, Moto Roader, etc.
- **Button modes** -- 2-button, 3-button (Street Fighter II), and 6-button modes handled automatically.
- **Turbo** -- Buttons III and IV have built-in auto-fire (~15Hz).
- **Mouse support** -- USB mouse outputs native PCEngine mouse protocol for Afterburner II, Darius Plus, Lemmings.
- **Player shifting** -- When a controller disconnects, remaining players shift up to fill gaps.

## Supported Boards

| Board | Build Command |
|-------|---------------|
| KB2040 | `make usb2pce_kb2040` |

## Build and Flash

```bash
make usb2pce_kb2040
make flash-usb2pce_kb2040
```

## Compatible Games

### Mouse-Compatible
- Afterburner II
- Darius Plus
- Lemmings

### Multitap-Compatible (5 players)
- Bomberman '93
- Bomberman '94
- Dungeon Explorer
- Moto Roader

## Troubleshooting

**Controller not responding:**
- Check PCEngine port connections, especially 5V power and ground.
- Verify data and select pin assignments match your board.

**Multitap not working:**
- Ensure the USB hub provides enough power for all controllers.
- Some games do not support 5-player mode.

**Mouse not working:**
- Verify the game supports the PCEngine mouse.
- Check that the USB mouse is detected by the adapter.
- Try a different USB mouse model.

**Button mapping wrong:**
- Verify 2/3/6-button mode in the game's settings -- some games expect specific button layouts.
