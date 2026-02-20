# Casio Loopy Adapter (Experimental)

USB controller adapter for the Casio Loopy console.

**Status: Experimental** - Protocol is partially implemented.

## Features

### Controller Support

- **Players**: Up to 4
- **Input Types**: Standard controller
- **Button Layout**: D-pad + action buttons

### Current Limitations

- Protocol partially implemented
- Some timing issues may exist
- Limited game testing

## Button Mappings

### Standard Controller

| USB Input | Loopy Output |
|-----------|--------------|
| B1 (Cross/B) | B |
| B2 (Circle/A) | A |
| B3 (Square/X) | C |
| B4 (Triangle/Y) | D |
| S1 (Select) | Select |
| S2 (Start) | Start |
| D-Pad | D-Pad |

## Hardware Requirements

- **Board**: Adafruit KB2040 (default)
- **Protocol**: PIO-based serial
- **Status**: Experimental

### GPIO Pin Configuration

See source code for current pin assignments:
- `src/native/device/loopy/loopy_device.c`
- `src/native/device/loopy/loopy.pio`

## Casio Loopy Overview

The Casio Loopy (1995) was a Japan-only console aimed at young girls:
- 32-bit SH-1 processor
- Unique thermal sticker printer
- Limited game library (~10 titles)
- Controller uses custom serial protocol

## Compatible Games

- Anime Land
- Bow-wow Puppy Love Story
- Dream Change
- HARIHARI Seal Paradise
- Little Romance
- Lupiton's Wonder Palette
- Magical Shop
- PC Collection
- Wanwan Aijou Monogatari

## Troubleshooting

**Controller not responding:**
- Check cable connections
- Verify power supply
- Experimental status - may have timing issues

**Buttons mapped incorrectly:**
- Button mapping is best-effort
- Limited documentation on Loopy protocol
- Community feedback welcome

## Development Status

The Loopy adapter is experimental:
- Basic protocol implemented via PIO
- Limited testing with actual hardware
- Pull requests welcome for improvements

## Contributing

If you have a Casio Loopy and can help test:
- Join Discord: [community.joypad.ai](http://community.joypad.ai/)
- Report issues on GitHub
- Protocol documentation appreciated

## Product Links

- **Pre-built hardware**: Not currently available (experimental)
- [GitHub Releases](https://github.com/RobertDaleSmith/Joypad/releases) - Latest firmware
