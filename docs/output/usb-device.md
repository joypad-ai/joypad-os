# USB Device Output Interface

Emulates various USB gamepads, keyboards, and mice. Any connected input controller (USB, Bluetooth, WiFi, or native) is translated into the selected USB output protocol. Supports 14 output modes selectable at runtime, with mode persistence across power cycles.

## Protocol

- **Wire protocol**: USB 2.0 (native RP2040 USB controller, no PIO needed)
- **Core**: Runs on Core 0 (not timing-critical)
- **Stack**: TinyUSB device stack

No PIO state machines or overclock required. The adapter enumerates as a composite USB device with CDC (serial configuration) and HID (gamepad/keyboard/mouse) interfaces.

## Output Modes

### Mode Cycling

- **Double-click** the board button to cycle through primary modes
- **Triple-click** to reset to SInput (default)
- Mode can also be changed via CDC command (`MODE.SET`) or [web config](../features/web-config.md)
- Switching triggers USB re-enumeration (brief disconnect)
- Selected mode persists to flash

**Cycle order:** SInput -> XInput -> PS3 -> PS4 -> Switch -> Keyboard/Mouse -> SInput

### All Modes

| Mode | Constant | Emulates | VID:PID | Primary Use |
|------|----------|----------|---------|-------------|
| HID | `USB_OUTPUT_MODE_HID` | Generic DInput Gamepad | 2563:0575 | Legacy, replaced by SInput |
| SInput | `USB_OUTPUT_MODE_SINPUT` | Joypad HID Gamepad | 2E8A:10C6 | PC/Mac/Linux (default, Steam-compatible) |
| XInput | `USB_OUTPUT_MODE_XINPUT` | Xbox 360 Controller | 045E:028E | PC and Xbox 360 console |
| PS3 | `USB_OUTPUT_MODE_PS3` | DualShock 3 | 054C:0268 | PC and PS3 console |
| PS4 | `USB_OUTPUT_MODE_PS4` | DualShock 4 | 054C:05C4 | PC (console requires auth dongle) |
| Switch | `USB_OUTPUT_MODE_SWITCH` | Pro Controller | 057E:2009 | Nintendo Switch (docked USB) |
| PS Classic | `USB_OUTPUT_MODE_PSCLASSIC` | PS Classic Controller | -- | PlayStation Classic mini console |
| Xbox Original | `USB_OUTPUT_MODE_XBOX_ORIGINAL` | Controller S | 045E:0289 | Original Xbox (XID protocol) |
| Xbox One | `USB_OUTPUT_MODE_XBONE` | Xbox One Controller | -- | Xbox One/Series (GIP protocol) |
| XAC | `USB_OUTPUT_MODE_XAC` | Xbox Adaptive Controller | -- | Accessibility |
| KB/Mouse | `USB_OUTPUT_MODE_KEYBOARD_MOUSE` | HID Keyboard + Mouse | -- | Desktop / accessibility |
| GC Adapter | `USB_OUTPUT_MODE_GC_ADAPTER` | Wii U GC Adapter | -- | Wii U / Switch GameCube mode |
| PCE Mini | `USB_OUTPUT_MODE_PCEMINI` | PC Engine Mini Controller | -- | TurboGrafx-16 Mini |
| CDC | `USB_OUTPUT_MODE_CDC` | CDC-only (no HID) | 2E8A:10C7 | Serial configuration only |

### Feature Support by Mode

| Mode | Rumble | Player LED | RGB | Motion | Auth |
|------|--------|------------|-----|--------|------|
| SInput | L+R | -- | -- | Gyro/Accel | -- |
| XInput | L+R | 1-4 | -- | -- | XSM3 (Xbox 360) |
| PS3 | L+R | 1-7 | -- | Gyro/Accel | -- |
| PS4 | L+R | -- | Lightbar | -- | Passthrough |
| Switch | L+R | 1-7 | -- | -- | -- |
| KB/Mouse | -- | -- | -- | -- | -- |

Feedback (rumble, LED, RGB) is forwarded back to the connected input controller via the player manager.

### SInput (Default)

SInput is the default mode. It uses a Joypad-specific HID descriptor with:
- Standard gamepad buttons, sticks, triggers
- Gyroscope and accelerometer reports (when input controller provides them)
- Face button style reporting for SDL/Steam compatibility
- Composite device: Gamepad (ITF 0) + Keyboard (ITF 1) + Mouse (ITF 2)

### Xbox 360 Console Compatibility

XInput mode works on real Xbox 360 hardware. The adapter authenticates using XSM3 (Xbox Security Method 3) via [libxsm3](https://github.com/InvoxiPlayGames/libxsm3). Authentication completes in approximately 2 seconds (LED transitions from blinking to solid).

## Player Support

- **Max players**: 1 per USB device output (one gamepad report)
- **Multi-controller**: Multiple input controllers merge to one output via router

## Button Mapping

Buttons are mapped from `JP_BUTTON_*` to mode-specific USB report formats. The SInput/HID mapping uses DirectInput ordering:

| JP_BUTTON_* | DInput Button # | USB Mask |
|-------------|-----------------|----------|
| `JP_BUTTON_B3` | 1 | Bit 0 |
| `JP_BUTTON_B1` | 2 | Bit 1 |
| `JP_BUTTON_B2` | 3 | Bit 2 |
| `JP_BUTTON_B4` | 4 | Bit 3 |
| `JP_BUTTON_L1` | 5 | Bit 4 |
| `JP_BUTTON_R1` | 6 | Bit 5 |
| `JP_BUTTON_L2` | 7 | Bit 6 |
| `JP_BUTTON_R2` | 8 | Bit 7 |
| `JP_BUTTON_S1` | 9 | Bit 8 |
| `JP_BUTTON_S2` | 10 | Bit 9 |
| `JP_BUTTON_L3` | 11 | Bit 10 |
| `JP_BUTTON_R3` | 12 | Bit 11 |
| `JP_BUTTON_A1` | 13 | Bit 12 |

D-pad is reported as a 4-bit hat direction. Each mode remaps to its target console's expected button order internally.

## Analog Mapping

| Input | USB Output | Range |
|-------|------------|-------|
| Left stick X/Y | Left analog | 0-255 (8-bit) or 0-65535 (16-bit, mode dependent) |
| Right stick X/Y | Right analog | Same |
| L2/R2 analog | Trigger axes | 0-255 |

## Feedback

Feedback varies by mode:

- **Rumble**: Dual motor (left heavy, right light) for XInput, PS3, PS4, Switch, SInput. Forwarded to input controller via player manager.
- **Player LED**: XInput and PS3/Switch assign player numbers (1-4 or 1-7). Forwarded to controllers with player LED support.
- **RGB LED**: PS4 lightbar color forwarded to DualSense/DS4 controllers.

## CDC Configuration Interface

Two CDC serial ports are exposed:
- **Port 0 (Data)**: JSON command protocol for mode switching, profile management, input monitoring, rumble testing, and device settings
- **Port 1 (Debug)**: Printf logging output

The web configuration interface at [config.joypad.ai](https://config.joypad.ai) communicates over Port 0 using WebSerial.

### CDC Command Reference

Commands are JSON objects sent over CDC port 0. Format: `{"cmd":"COMMAND.NAME", ...params}`

| Command | Description |
|---------|-------------|
| `INFO` | Get device info (app, version, board, serial) |
| `PING` | Connectivity check |
| `REBOOT` | Restart the adapter |
| `BOOTSEL` | Reboot into UF2 flash mode |
| `MODE.GET` | Get current output mode |
| `MODE.SET` | Set output mode (triggers re-enumeration) |
| `MODE.LIST` | List all available modes |
| `PROFILE.LIST` | List button remapping profiles |
| `PROFILE.GET` | Get profile details |
| `PROFILE.SET` | Create/update a profile |
| `PROFILE.SAVE` | Save profiles to flash |
| `PROFILE.DELETE` | Delete a profile |
| `PROFILE.CLONE` | Duplicate a profile |
| `INPUT.STREAM` | Toggle real-time input event streaming |
| `SETTINGS.GET` | Get device settings |
| `SETTINGS.RESET` | Reset settings to defaults |
| `PLAYERS.LIST` | List connected controllers |
| `RUMBLE.TEST` | Send test rumble to a player |
| `RUMBLE.STOP` | Stop rumble on a player |
| `BT.STATUS` | Bluetooth connection status (BT builds only) |
| `BT.BONDS.CLEAR` | Clear all Bluetooth pairings (BT builds only) |

## Profiles

USB device output does not have console-specific profiles. Button remapping is handled by the profile system at the app/core level.

## Apps Using This Output

| App | Description |
|-----|-------------|
| `usb2usb` | USB/BT controllers to USB HID |
| `bt2usb` | Bluetooth-only to USB HID (Pico W, ESP32-S3, nRF52840) |
| `wifi2usb` | WiFi (JOCP) to USB HID (Pico W) |
| `snes2usb` | SNES controllers to USB HID |
| `n642usb` | N64 controllers to USB HID |
| `gc2usb` | GameCube controllers to USB HID |
| `nes2usb` | NES controllers to USB HID |
| `neogeo2usb` | Neo Geo controllers to USB HID |
| `lodgenet2usb` | LodgeNet controllers to USB HID |
| `nuon2usb` | Nuon controllers to USB HID (experimental) |
| `controller_*` | Custom GPIO controllers to USB HID |
