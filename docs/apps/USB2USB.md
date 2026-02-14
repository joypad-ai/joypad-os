# USB Output Interface

The USB output interface lets Joypad adapters emulate various USB gamepads. Any connected controller (USB, Bluetooth, WiFi, or native) is translated into the selected output protocol.

Used by: `usb2usb`, `bt2usb`, `wifi2usb`, `snes2usb`, `n642usb`, `gc2usb`, `controller`

## Web Configuration

Connect the adapter to a computer and open **[config.joypad.ai](https://config.joypad.ai)** in a WebSerial-capable browser (Chrome, Edge). The web app communicates over the adapter's CDC serial interface to provide:

- **Mode switching** - Change USB output mode without button combos
- **Profile management** - Create, edit, and assign button remapping profiles
- **Input monitor** - Real-time view of controller inputs and processed outputs
- **Rumble test** - Test vibration on connected controllers
- **Player management** - View connected controllers and player assignments
- **Bluetooth management** - View bond status, clear pairings (BT-capable boards)
- **Settings** - View/reset device settings
- **Firmware update** - Reboot into BOOTSEL for drag-and-drop UF2 flashing

The CDC protocol uses JSON commands over a framed binary transport. Two CDC ports are exposed: Data (commands/responses) and Debug (printf logging).

## Output Modes

### Mode Cycling

- **Double-click** the board button to cycle modes
- **Triple-click** to reset to SInput (default)
- Mode is saved to flash and persists across power cycles
- Switching triggers USB re-enumeration (brief disconnect)

**Cycle order:** SInput -> XInput -> PS3 -> PS4 -> Switch -> Keyboard/Mouse -> SInput

### Mode Reference

| Mode | Emulates | VID:PID | Use Case |
|------|----------|---------|----------|
| **SInput** | Joypad HID Gamepad | 2E8A:10C6 | PC/Mac/Linux (default, Steam-compatible) |
| **XInput** | Xbox 360 Controller | 045E:028E | PC and **Xbox 360 console** |
| **PS3** | DualShock 3 | 054C:0268 | PC and PS3 console |
| **PS4** | DualShock 4 | 054C:05C4 | PC (console requires auth dongle) |
| **Switch** | Pro Controller | 057E:2009 | Nintendo Switch (docked USB) |
| **Keyboard/Mouse** | HID KB + Mouse | - | Accessibility / desktop use |

Additional modes (not in cycle, accessible via web config):

| Mode | Emulates | Use Case |
|------|----------|----------|
| **PS Classic** | PS Classic Controller | PlayStation Classic mini console |
| **Xbox Original** | Xbox Controller S | Original Xbox console |
| **Xbox One** | Xbox One Controller | Xbox One/Series (GIP protocol) |
| **XAC** | Xbox Adaptive Controller | Accessibility |
| **GC Adapter** | Wii U GC Adapter | Wii U / Switch GameCube mode |
| **HID** | Generic DInput | Legacy, replaced by SInput |

### Feature Support

| Mode | Rumble | Player LED | RGB | Motion | Auth |
|------|--------|------------|-----|--------|------|
| SInput | L+R | - | - | Gyro/Accel | - |
| XInput | L+R | 1-4 | - | - | XSM3 (Xbox 360) |
| PS3 | L+R | 1-4 | - | Gyro/Accel | - |
| PS4 | L+R | - | Lightbar | - | Passthrough |
| Switch | - | - | - | - | - |
| KB/Mouse | - | - | - | - | - |

Feedback (rumble, LED, RGB) is forwarded back to the connected input controller. For example, Xbox 360 player LED assignment will set the corresponding color on a DualSense lightbar.

## Xbox 360 Console Compatibility

XInput mode works on real Xbox 360 hardware. The adapter authenticates using XSM3 (Xbox Security Method 3), the same protocol used by licensed wired controllers.

**What works:**
- Full gamepad input (buttons, sticks, triggers)
- Rumble feedback
- Player LED assignment
- Guide button

**How it works:**
The adapter presents a full 4-interface Xbox 360 controller descriptor (gamepad, audio stub, plugin stub, security). On connection, the console initiates an XSM3 challenge-response handshake over vendor control requests. The adapter handles this using [libxsm3](https://github.com/InvoxiPlayGames/libxsm3). Authentication completes in ~2 seconds (LED goes from blinking to solid).

## CDC Command Reference

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
| `BT.STATUS` | Bluetooth connection status (BT builds) |
| `BT.BONDS.CLEAR` | Clear all Bluetooth pairings (BT builds) |
