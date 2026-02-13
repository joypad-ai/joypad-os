# Installation & Flashing Guide

How to update firmware on your Joypad adapter.

## Pre-Built Hardware

### Joypad Dev Kits (Recommended)

The easiest way to get started without DIY. Upcoming dev kits:
- **USB2USB** — USB controller passthrough adapter
- **BT2USB** — Bluetooth-to-USB controller adapter

Visit [joypad.ai/products](https://joypad.ai/products) for current availability.

### Legacy Controller Adapter Hardware

The following Controller Adapter products are fully compatible with joypad-os firmware:
- USB-2-PCE — PCEngine/TurboGrafx-16
- GC USB — GameCube/Wii
- NUON USB — Nuon DVD Players
- USB-2-3DO — 3DO Interactive Multiplayer

These legacy devices can be flashed with the latest joypad-os firmware using the instructions below.

## Downloading Firmware

1. Go to [GitHub Releases](https://github.com/joypad-ai/joypad-os/releases)
2. Download the latest `.uf2` file for your product:
   - `joypad_<commit>_usb2pce_kb2040.uf2` - PCEngine adapter
   - `joypad_<commit>_usb2gc_kb2040.uf2` - GameCube adapter
   - `joypad_<commit>_usb2nuon_kb2040.uf2` - Nuon adapter
   - `joypad_<commit>_usb23do_rp2040zero.uf2` - 3DO adapter
   - `joypad_<commit>_usb2loopy_kb2040.uf2` - Casio Loopy adapter (experimental)

## Flashing Instructions

### Standard Method (Most Adapters)

Works for: USB-2-PCE, NUON USB, USB-2-3DO, USB-2-Loopy

1. **Prepare adapter**
   - Disconnect adapter from console
   - Disconnect all USB devices from adapter

2. **Enter bootloader mode**
   - Hold **BOOT** button on adapter
   - While holding BOOT, connect USB-C cable to computer
   - Release BOOT button
   - A drive named `RPI-RP2` should appear

3. **Flash firmware**
   - Drag and drop the `.uf2` file onto the `RPI-RP2` drive
   - Wait for file to copy (usually instant)
   - Drive will automatically eject

4. **Verify**
   - Adapter LED should light up
   - Reconnect to console
   - Connect USB controller and test

### GC USB (No BOOT Button Method)

The GC USB adapter automatically enters bootloader mode when powered on without a GameCube console connected.

1. **Prepare adapter**
   - Disconnect adapter from GameCube console
   - Disconnect all USB devices from adapter

2. **Enter bootloader mode**
   - Connect USB-C cable to computer (no button press needed)
   - A drive named `RPI-RP2` should appear

3. **Flash firmware**
   - Drag and drop `.uf2` onto the `RPI-RP2` drive
   - Wait for file to copy
   - Drive will automatically eject

4. **Verify**
   - Adapter LED should light up
   - Reconnect to GameCube
   - Connect USB controller and test

## Troubleshooting

### RPI-RP2 Drive Doesn't Appear

**On Windows:**
- Check Device Manager for "RP2 Boot" device
- Try a different USB port
- Try a different USB cable
- Restart computer

**On macOS:**
- Check Disk Utility for unmounted drive
- Try a different USB port
- Try a different USB cable (must be data cable, not charge-only)

**On Linux:**
- Run `lsusb` to check for "Raspberry Pi RP2 Boot"
- Check `dmesg` for mount errors
- May need to mount manually: `sudo mount /dev/sdX /mnt`

### BOOT Button Not Working

- Make sure you're holding BOOT **before** connecting USB
- Hold BOOT for full 3 seconds while connecting
- Try different USB cable
- Check if BOOT button is physically functional

### Firmware Flash Fails

- Check that `.uf2` file isn't corrupted (re-download)
- Ensure enough disk space (firmware is ~500KB)
- Don't rename the `.uf2` file
- Try copying via command line instead of drag-drop

### Adapter Not Working After Flash

- Verify you flashed the correct firmware for your product
- Try reflashing firmware
- Check all cable connections to console
- Test with known-good USB controller
- Check adapter LED status

## LED Status Indicators

**During normal operation:**
- **Solid Green** - No controllers connected
- **Solid Blue** - 1 controller connected
- **Purple** - 2 controllers connected
- **Red** - 3+ controllers connected

**During profile change:**
- **Yellow flash** - Profile changed
- Controller will also rumble if supported

**Bootloader mode:**
- No LED (RPI-RP2 drive appears on computer)

## Profile Switching

Several adapters support switchable button mapping profiles:

### GameCube (USB2GC)
- Hold **Select** for 2 seconds
- Then press **D-Pad Up/Down** to cycle profiles
- Profiles: Default, SNES, SSBM, MKWii, Fighting
- See [GameCube docs](consoles/GAMECUBE.md) for details

### 3DO (USB23DO)
- Hold **Select** for 2 seconds
- Then press **D-Pad Up/Down** to cycle profiles
- Profiles: Default, Fighting, Shooter
- See [3DO docs](consoles/3DO.md) for details

### Nuon (USB2Nuon)
- **In-Game Reset (IGR)**: Hold L1+R1+Start+Select
  - Tap: Stop button (return to menu)
  - Hold 2 seconds: Power button (power off player)
- See [Nuon docs](consoles/NUON.md) for details

## Verifying Firmware Version

After flashing, you can verify the firmware version:

1. Connect adapter to console
2. Check serial output via UART (developers only)
3. Or test functionality with your specific console

Console-specific features will match the version in the release notes.

## Downgrading Firmware

You can flash any previous firmware version:

1. Download older `.uf2` from [Releases](https://github.com/joypad-ai/joypad-os/releases)
2. Follow same flashing procedure
3. No need to erase current firmware first

## Factory Reset

To reset adapter to default settings:

1. Flash the latest firmware (this resets all settings)
2. Or manually clear flash memory (developers only - requires recompile)

**Note**: Adapters with profiles store profile selection in flash. Reflashing firmware does **not** erase saved profile (by design). To reset profile to default, switch to profile 0 (Up on D-Pad during profile change).

## Getting Help

- **Discord**: [community.joypad.ai](http://community.joypad.ai/) - Community support
- **Issues**: [GitHub Issues](https://github.com/joypad-ai/joypad-os/issues) - Bug reports
- **Email**: support@joypad.ai - Product support
