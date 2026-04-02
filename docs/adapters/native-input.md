> **Note:** This page is being replaced by the new layer-based documentation.
> See [SNES Input](../input/snes.md), [N64 Input](../input/n64.md), [GameCube Input](../input/gamecube.md), [LodgeNet Input](../input/lodgenet.md),
> and apps: [SNES2USB](../apps/snes2usb.md), [N642USB](../apps/n642usb.md), [GC2USB](../apps/gc2usb.md), [LodgeNet2USB](../apps/lodgenet2usb.md).

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

## LodgeNet to USB (lodgenet2usb)

Convert Nintendo LodgeNet hotel gaming controllers into USB HID gamepads. Supports all three LodgeNet controller variants with automatic detection.

### Features

- Auto-detection between N64, GameCube, and SNES LodgeNet controllers
- Hot-swap between controller types (no reboot needed)
- Full analog support (N64 stick, GC sticks + triggers)
- LodgeNet system buttons (Menu, Order, Select, Plus, Minus) mapped to extended buttons
- SInput face style reporting (Nintendo/GameCube layout detected automatically)
- Onboard LED indicator (blinks when idle, solid when connected)
- USB output mode switching (XInput, DInput, Switch, PS3, PS4, SInput)
- Web configuration via [joypad.ai](https://joypad.ai)

### Hardware

- **Board**: Pico or Pico 2
- **Protocol**: PIO-based (MCU protocol for N64/GC at ~60Hz, SR protocol for SNES at ~131Hz)
- **Connector**: RJ11 6P6C (6-pin, but only 4 used for game port)
- **Build**: `make lodgenet2usb_pico` or `make lodgenet2usb_pico2`

### LodgeNet Connector

The LodgeNet system uses a 6-pin RJ11-style connector. The GAME port connector is what the controller plugs into (diagram via [Nielk1](https://x.com/Nielk1)):

```
 ╔═╤═╤═╤═╤═╤═╤═╗       GAME        TV                MTI
 ║ │ │ │ │ │ │ ║   1   DATA(in)    CLK(out)          IR(in)
 ║ 1 2 3 4 5 6 ║   2   CLK(out)    MTI(in)           GND
 ║             ║   3   12V         N/C               DATA(in)
 ║             ║   4   CLK2(out)   DATA(out)         N/C
 ╚════╤═══╤════╝   5   GND         GND               MTI(out)
      └───┘        6   IR          IR(out)           CLK(in)
```

For the adapter, only 4 lines from the GAME port are needed:

| RJ11 Pin | Signal | Description |
|---|---|---|
| 1 | DATA | Controller data output (active-low, pull-up) |
| 2 | CLK | Host clock output (idle HIGH) |
| 3 | 12V | Power input (use 5V from Pico VBUS instead) |
| 5 | GND | Ground |

Pin 4 (CLK2) is only used by SNES LodgeNet controllers. Pin 6 (IR) is for the TV remote interface and unused by the adapter.

### GPIO Pinout (Pico / Pico 2)

| GPIO | Function |
|---|---|
| 2 | DATA (input, pull-up) ← RJ11 pin 1 |
| 3 | CLK1 (output, idle HIGH) ← RJ11 pin 2 |
| 4 | VCC (output, always HIGH) — powers controller |
| 5 | CLK2 (output, SNES SR protocol only) ← RJ11 pin 4 |

### Button Mapping — N64 LodgeNet

| LodgeNet N64 | USB Output |
|---|---|
| A | B1 |
| C-Down | B2 |
| B | B3 |
| C-Left | B4 |
| Z | R1 |
| L | L2 |
| R | R2 |
| C-Up | L3 |
| C-Right | R3 |
| Start | S2 |
| D-Pad | D-Pad |
| Stick | Left Analog (scaled ±80 → full range) |
| Menu (U+D) | A1 (Home) |
| Select (U+D+R) | S1 (Back) |

### Button Mapping — GameCube LodgeNet

| LodgeNet GC | USB Output |
|---|---|
| A | B2 |
| B | B1 |
| X | B4 |
| Y | B3 |
| Z | R1 |
| L | L2 |
| R | R2 |
| Start | S2 |
| D-Pad | D-Pad |
| Main Stick | Left Analog |
| C-Stick | Right Analog |
| L Trigger | L2 (analog) |
| R Trigger | R2 (analog) |
| Menu (U+D) | A1 (Home) |
| Select (U+D+R) | S1 (Back) |

### Button Mapping — SNES LodgeNet

| LodgeNet SNES | USB Output |
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
| Menu | A1 (Home) |
| Order | A2 (Capture) |
| Minus (U+D) | A3 |
| Plus (L+R) | A4 |

### Protocol Details

LodgeNet controllers use a proprietary 3-wire serial protocol over RJ11 connectors, originally used in Nintendo hotel gaming systems. Two protocol families exist:

- **MCU Protocol** (N64/GC): Hello pulse sequence (2x 7us LOW pulses), then 80 bits clocked MSB-first. Byte 1 flags distinguish N64 (bit 6 clear) from GC (bit 6 set). Polled at ~60Hz.
- **SR Protocol** (SNES): Dual-clock shift register. 16 data bits + 1 presence bit using CLK1 (side-set) and CLK2 (set pin). Polled at ~131Hz.

Auto-detection cycles between protocols: MCU fails 5x → switch to SR, SR fails 5x → switch to MCU. MCU requires 15 consecutive good reads before outputting to prevent connection flash.

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

All native-to-USB adapters (snes2usb, n642usb, gc2usb, neogeo2usb, lodgenet2usb) share the same [USB output interface](usb.md), including:

- Multiple USB output modes (XInput, DInput, Switch, PS3, PS4)
- Web configuration at [config.joypad.ai](https://config.joypad.ai)
- CDC serial commands
- Mode saved to flash (persists across power cycles)
