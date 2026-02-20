# Nuon DVD Player Adapter

USB controller adapter for Nuon DVD game players with spinner and In-Game Reset (IGR) support.

## Features

### Standard Controller

Full Nuon controller emulation:
- D-Pad navigation
- 8 digital buttons (A, B, Start, Nuon, C-Up, C-Down, C-Left, C-Right)
- L/R shoulder buttons
- Full button mapping from modern controllers

### Spinner Controller (Tempest 3000)

USB mouse → Nuon spinner emulation:
- X-axis mouse movement → Spinner rotation
- Precise control for Tempest 3000
- Auto-detects when mouse connected
- Adjustable sensitivity

### In-Game Reset (IGR)

Reset or power off your Nuon player without getting up:

**Button Combo**: Hold **L1 + R1 + Start + Select**

- **Tap** (release before 2 seconds): Triggers **Stop** button
  - Returns to DVD menu in most players
- **Hold** (2+ seconds): Triggers **Power** button
  - Powers off the Nuon player

The IGR uses the hotkeys service for reliable combo detection with debouncing.

## Button Mappings

### Standard Controller

| USB Input | Nuon Output |
|-----------|-------------|
| B1 (Cross/B) | A |
| B2 (Circle/A) | C-Down |
| B3 (Square/X) | B |
| B4 (Triangle/Y) | C-Left |
| L1 (LB/L) | L |
| R1 (RB/R) | R |
| L2 (LT/ZL) | C-Up |
| R2 (RT/ZR) | C-Right |
| S1 (Select) | Nuon |
| S2 (Start) | Start |
| A2 (Touchpad/Capture) | Nuon |
| D-Pad | D-Pad |
| Left Stick | D-Pad |

### Spinner Mode (USB Mouse)

| USB Mouse | Nuon Spinner |
|-----------|--------------|
| X Movement | Spinner Rotation |
| Left Click | Fire (A button) |
| Right Click | - |

**Tempest 3000 Controls:**
- Mouse left/right → Spinner rotation
- Left click → Fire
- Keyboard/Controller → Secondary buttons

## Hardware Requirements

- **Board**: Adafruit KB2040 (default), Pico, QT Py, Waveshare
- **Protocol**: Polyface serial protocol via PIO
- **GPIO Pins**: See source code for assignments

### PIO Programs

- `polyface_read.pio` - Read commands from console
- `polyface_send.pio` - Send controller data to console

## Nuon Polyface Protocol

Nuon uses a custom bidirectional serial protocol called "Polyface":
- Bidirectional communication on data line
- Console polls controller at ~60Hz
- Controller responds with button state packet
- Supports hot-plugging

## Spinner Sensitivity

Default spinner sensitivity is optimized for Tempest 3000:
- 1:1 mouse movement to rotation
- Linear response (no acceleration)
- Adjustable via `NUON_SPINNER_SCALE` in firmware

**Tips for Best Spinner Experience:**
- Use optical mouse (not ball mouse)
- Lower DPI settings may feel more natural
- Some gaming mice have on-the-fly DPI adjustment

## Compatible Nuon Players

- Samsung DVD-N501
- Samsung DVD-N504 / N505
- Toshiba SD-2300
- Motorola Streamaster 5000
- RCA DRC300N / DRC480N

## Compatible Games

### Standard Controller:
- Iron Soldier 3
- Ballistic
- Space Invaders XL
- Merlin Racing
- Freefall 3050 A.D.
- The Next Tetris

### Spinner (Tempest 3000):
- **Tempest 3000** - Premium spinner experience with USB mouse
- VLM-2 (audio visualizer)

## Troubleshooting

**Controller not detected:**
- Check Nuon port connections
- Verify power and ground pins
- Check data pins match protocol

**IGR not working:**
- Hold all four buttons (L1+R1+Start+Select) simultaneously
- Tap for Stop, hold 2+ seconds for Power
- Some Nuon players may not respond to all functions

**Spinner too sensitive/slow:**
- Adjust mouse DPI settings
- Modify `NUON_SPINNER_SCALE` in firmware
- Try different USB mouse

**Buttons not responding:**
- Verify button mapping matches game expectations
- Check USB controller is detected
- Test with known-good controller

**Tempest 3000 spinner issues:**
- Use optical mouse (not ball mouse)
- Check mouse polling rate
- Verify USB mouse is detected
- Try lower DPI setting

## Special Notes

- Nuon protocol is timing-sensitive (uses PIO state machines)
- Mouse input provides best Tempest 3000 experience
- Some Nuon players may have voltage differences (3.3V vs 5V)
- IGR combo requires holding all buttons before timer starts

## Product Links

- [NUON USB Adapter](https://controlleradapter.com/products/nuon-usb) - Pre-built hardware
- [GitHub Releases](https://github.com/RobertDaleSmith/Joypad/releases) - Latest firmware
