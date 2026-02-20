# PCEngine / TurboGrafx-16 Adapter

USB controller and mouse adapter for PCEngine/TurboGrafx-16 with multitap support.

## Features

### 🎮 Controller Support

- **2-button mode** - Standard PCEngine controller (I, II)
- **3-button mode** - Street Fighter II layout (I, II, Select as III)
- **6-button mode** - Full button support (I-VI)
- **Turbo buttons** - Auto-fire on buttons III and IV

### 🖱️ Mouse Support

Full PCEngine Mouse emulation for compatible games:
- Afterburner II
- Darius Plus
- And other mouse-compatible titles

**Using USB Mouse:**
- Connect USB mouse to adapter
- Mouse automatically maps to PCEngine mouse protocol
- No configuration needed

### 👥 Multitap (1-5 Players)

Supports up to 5 players via PCEngine multitap:
- Connect up to 5 USB controllers via USB hub
- Each controller automatically assigns to next available slot
- Works with all multitap-compatible games

**Player Assignment:**
- Players assigned in order of connection
- First connected = Player 1
- Player slots persist until disconnect
- No shifting on disconnect (remaining players keep their slots)

## Button Mappings

### Standard Controller

| USB Input | PCEngine Output |
|-----------|-----------------|
| B1 (A/Cross) | II |
| B2 (B/Circle) | I |
| B3 (X/Square) | IV (Turbo II) |
| B4 (Y/Triangle) | III (Turbo I) |
| L1 (LB/L) | VI |
| R1 (RB/R) | V |
| S1 (Back/Select) | Select |
| S2 (Start) | Run |
| D-Pad | D-Pad |

### 2-Button Mode
- I, II only
- Other buttons ignored

### 3-Button Mode (Street Fighter II)
- I, II, Select (mapped to L2/R2 or Select button)
- Useful for SFII Championship Edition

### 6-Button Mode
- All six buttons active (I-VI)
- Turbo on III (Turbo I) and IV (Turbo II)

### Mouse Mapping

| USB Mouse | PCEngine Mouse |
|-----------|----------------|
| Left Click | Left Button |
| Right Click | Right Button |
| Movement | Movement (1:1) |
| Scroll Wheel | (not mapped) |

## Hardware Requirements

- **Board**: Adafruit KB2040 (default), also supports Pico, QT Py, Waveshare RP2040-Zero
- **Protocol**: Uses PIO state machines for precise timing
  - `plex.pio` - Data multiplexing
  - `clock.pio` - Clock generation
  - `select.pio` - Controller select
- **Connector**: PCEngine controller port (8-pin DIN)

## Pin Configuration

### PCEngine Controller Port (8-pin DIN)

```
Pin 1: VCC (5V)
Pin 2: D0  (Up/I)
Pin 3: D1  (Right/II)
Pin 4: D2  (Down/Select)
Pin 5: D3  (Left/Run)
Pin 6: SEL (Select — nibble toggle from console)
Pin 7: CLR (Clear/OE — scan reset from console)
Pin 8: GND
```

> **Note:** Pin 7 is labeled **OE** (Output Enable) in some references and **CLR** (Clear) in others. They are the same signal.

### Wiring — KB2040 (default)

| PCE Pin | Signal | KB2040 GPIO |
|---------|--------|-------------|
| 1 | VCC (5V) | VBUS |
| 2 | D0 | GP26 |
| 3 | D1 | GP27 |
| 4 | D2 | GP28 |
| 5 | D3 | GP29 |
| 6 | SEL | GP18 |
| 7 | CLR/OE | GP19 |
| 8 | GND | GND |

### Wiring — Pico

| PCE Pin | Signal | Pico GPIO |
|---------|--------|-----------|
| 1 | VCC (5V) | VBUS |
| 2 | D0 | GP4 |
| 3 | D1 | GP5 |
| 4 | D2 | GP6 |
| 5 | D3 | GP7 |
| 6 | SEL | GP18 |
| 7 | CLR/OE | GP19 |
| 8 | GND | GND |

### Code Variable Naming

The source code uses legacy names for the input pins:
- `DATAIN_PIN` (GP18) = **SEL** — the select/nibble toggle line
- `CLKIN_PIN` (GP19) = **CLR/OE** — named "clock in" because `clock.pio` monitors its edges for scan timing

See [PCEngine Protocol Reference](../protocols/PCENGINE.md) for full technical details.

## Multitap Details

PCEngine multitap scanning:
- Scans all 5 ports sequentially
- 60Hz scan rate per port
- Each controller responds on its assigned slot
- Automatic detection of connected controllers

## Special Features

### Auto-Detection

- Controllers auto-detected on connection
- Mouse/controller automatically determined
- No manual mode switching required

### Turbo Functionality

- Buttons III and IV have built-in turbo
- Turbo rate: ~15Hz (configurable in firmware)
- Always active when button held

## Troubleshooting

**Controller not responding:**
- Check PCEngine port connections
- Verify 5V power supply
- Check data and select pin assignments

**Multitap not working:**
- Ensure USB hub provides enough power
- Check that all controllers are detected
- Some games don't support 5-player mode

**Mouse not working:**
- Verify game supports PCEngine mouse
- Check mouse is detected via USB
- Try different USB mouse model

**Button mapping wrong:**
- Verify 2/3/6-button mode in game settings
- Some games expect specific button layouts

## Compatible Games

### Mouse-Compatible:
- Afterburner II
- Darius Plus
- Lemmings

### Multitap-Compatible (5 players):
- Bomberman '93
- Bomberman '94
- Dungeon Explorer
- Moto Roader
- And many more

## Product Links

- [USB-2-PCE Adapter](https://controlleradapter.com/products/usb-2-pce) - Pre-built hardware
- [GitHub Releases](https://github.com/RobertDaleSmith/Joypad/releases) - Latest firmware
