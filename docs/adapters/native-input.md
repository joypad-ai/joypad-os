# Native Input Adapters

Use retro controllers as USB gamepads, or bridge them to other consoles. These adapters read native controller protocols directly via PIO — no USB involved on the input side.

## SNES to USB (snes2usb)

Convert a SNES or NES controller into a USB HID gamepad.

### Features

- SNES controllers, NES controllers, SNES mouse, Xband keyboard
- Rumble support (via LRG protocol, if controller supports it)
- USB output mode switching (XInput, DInput, Switch, PS3, PS4)
- Web configuration via [config.joypad.ai](https://config.joypad.ai)

### Hardware

- **Board**: KB2040 (default)
- **Protocol**: SNESpad library (GPIO polling)
- **Build**: `make snes2usb_kb2040`

### Button Mapping

| SNES Button | USB Output |
|---|---|
| B | B1 |
| A | B2 |
| Y | B3 |
| X | B4 |
| L | L1 |
| R | R1 |
| Select | S1 |
| Start | S2 |
| D-Pad | D-Pad |

---

## N64 to USB (n642usb)

Convert an N64 controller into a USB HID gamepad.

### Features

- Full analog stick support
- Rumble pak detection and feedback
- Two button mapping profiles:
  - **Default**: A→B1, C-Down→B2, B→B3, C-Left→B4
  - **Dual Stick**: C-buttons map to right analog stick instead of buttons
- USB output mode switching

### Hardware

- **Board**: KB2040 (default)
- **Protocol**: Joybus via PIO (single-wire, GPIO 29)
- **Build**: `make n642usb_kb2040`

### Button Mapping (Default Profile)

| N64 Button | USB Output |
|---|---|
| A | B1 |
| C-Down | B2 |
| B | B3 |
| C-Left | B4 |
| L | L1 |
| R | R1 |
| Z | L2 |
| C-Up | L3 |
| C-Right | R3 |
| Start | S2 |
| D-Pad | D-Pad |
| Stick | Left Analog |

---

## GameCube to USB (gc2usb)

Convert a GameCube controller into a USB HID gamepad.

### Features

- Full analog stick and trigger support (main stick, C-stick, L/R triggers)
- Rumble motor feedback
- Three button mapping profiles:
  - **Default**: Standard mapping (A→B1, B→B2, X→B3, Y→B4)
  - **Xbox Layout**: A/B swapped
  - **Nintendo Layout**: X/Y swapped
- USB output mode switching

### Hardware

- **Board**: KB2040 (default)
- **Protocol**: Joybus via PIO (single-wire, GPIO 29)
- **Polling**: 125Hz (GameCube native rate)
- **Build**: `make gc2usb_kb2040`

### Button Mapping (Default Profile)

| GC Button | USB Output |
|---|---|
| A | B1 |
| B | B2 |
| X | B3 |
| Y | B4 |
| L | L1 |
| R | R1 |
| Z | R2 |
| Start | S2 |
| D-Pad | D-Pad |
| Main Stick | Left Analog |
| C-Stick | Right Analog |
| L Trigger | L2 (analog) |
| R Trigger | R2 (analog) |

---

## Neo Geo to USB (neogeo2usb)

Convert a Neo Geo arcade stick or controller into a USB HID gamepad.

### Features

- 4-6 button arcade sticks (buttons A-D, Select, K3)
- D-pad mode hotkeys (hold Coin+Start + direction for 2 seconds):
  - **Down**: D-pad mode (default)
  - **Left**: Left analog stick mode
  - **Right**: Right analog stick mode
- Coin+Start together acts as Home/Guide button
- USB output mode switching

### Hardware

- **Board**: KB2040 (default), RP2040-Zero
- **Protocol**: GPIO polling (active-low buttons with internal pull-ups)
- **Connector**: DB15 male
- **Build**: `make neogeo2usb_kb2040` or `make neogeo2usb_rp2040zero`

For wiring details, see the [Neo Geo adapter docs](neogeo.md#hardware-requirements) (same DB15 pinout).

---

## Cross-Console Adapters

These adapters bridge native retro controllers to other retro consoles:

### N64 to Dreamcast (n642dc)

- **Input**: N64 controller (joybus, GPIO 29)
- **Output**: Dreamcast Maple Bus (GPIO 2/3)
- **Board**: KB2040
- **Features**: Rumble feedback from Dreamcast forwarded to N64 rumble pak
- **Build**: `make n642dc_kb2040`

### SNES to 3DO (snes23do)

- **Input**: SNES/NES controller (GPIO polling)
- **Output**: 3DO PBUS protocol
- **Board**: RP2040-Zero
- **Features**: Mouse support, profile switching
- **Build**: `make snes23do_rp2040zero`

### N64 to Nuon (n642nuon)

- **Input**: N64 controller (joybus, GPIO 29)
- **Output**: Nuon polyface protocol
- **Board**: KB2040
- **Build**: `make n642nuon_kb2040`

---

## USB Output

All native-to-USB adapters (snes2usb, n642usb, gc2usb, neogeo2usb) share the same [USB output interface](usb.md), including:

- Multiple USB output modes (XInput, DInput, Switch, PS3, PS4)
- Web configuration at [config.joypad.ai](https://config.joypad.ai)
- CDC serial commands
- Mode saved to flash (persists across power cycles)
