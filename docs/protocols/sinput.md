# SInput Protocol

USB HID protocol reference for the SInput gamepad standard. Designed for SDL/Steam compatibility with rich feature reporting. Based on Handheld Legend's SInput HID specification.

Reference: https://docs.handheldlegend.com/s/sinput

## USB Identifiers

| Field | Value |
|-------|-------|
| VID | 0x2E8A (Raspberry Pi) |
| PID | 0x10C6 (SInput generic) |
| bcdDevice | 0x0100 (v1.0) |

## Report Descriptor Overview

Single HID collection (Game Pad) with three reports:

| Report ID | Direction | Size | Content |
|-----------|-----------|------|---------|
| 0x01 | Input | 64 bytes | Gamepad state (buttons, sticks, triggers, IMU, touchpad) |
| 0x02 | Input | 24 bytes | Feature response (capabilities, device info) |
| 0x03 | Output | 48 bytes | Commands (haptic, LED, feature request) |

## Input Report (ID 0x01, 64 bytes)

| Offset | Size | Field | Range |
|--------|------|-------|-------|
| 0 | 1 | Plug status | 0/1 (charging) |
| 1 | 1 | Charge level | 0-255 |
| 2-5 | 4 | Buttons | 32 buttons, little-endian |
| 6-7 | 2 | Left stick X | -32768 to 32767 (0 = center) |
| 8-9 | 2 | Left stick Y | -32768 to 32767 |
| 10-11 | 2 | Right stick X | -32768 to 32767 |
| 12-13 | 2 | Right stick Y | -32768 to 32767 |
| 14-15 | 2 | Left trigger | 0 to 32767 |
| 16-17 | 2 | Right trigger | 0 to 32767 |
| 18-21 | 4 | IMU timestamp | Microseconds (uint32) |
| 22-23 | 2 | Accel X | int16 |
| 24-25 | 2 | Accel Y | int16 |
| 26-27 | 2 | Accel Z | int16 |
| 28-29 | 2 | Gyro X | int16 |
| 30-31 | 2 | Gyro Y | int16 |
| 32-33 | 2 | Gyro Z | int16 |
| 34-39 | 6 | Touchpad 1 | X(2), Y(2), Pressure(2) |
| 40-45 | 6 | Touchpad 2 | X(2), Y(2), Pressure(2) |
| 46-62 | 17 | Reserved | Padding to 64 bytes |

### Button Mapping (32 bits, little-endian)

| Bit | Button | Bit | Button |
|-----|--------|-----|--------|
| 0 | East (B/Circle) | 16 | Start/Options |
| 1 | South (A/Cross) | 17 | Back/Select |
| 2 | North (Y/Triangle) | 18 | Guide/Home |
| 3 | West (X/Square) | 19 | Capture/Share |
| 4 | D-Up | 20 | Left Paddle 2 |
| 5 | D-Down | 21 | Right Paddle 2 |
| 6 | D-Left | 22 | Touchpad 1 Click |
| 7 | D-Right | 23 | Touchpad 2 Click |
| 8 | L3 (Left Stick) | 24 | Power |
| 9 | R3 (Right Stick) | 25-31 | Misc 4-10 |
| 10 | L1 (Left Bumper) | | |
| 11 | R1 (Right Bumper) | | |
| 12 | L2 (Left Trigger) | | |
| 13 | R2 (Right Trigger) | | |
| 14 | Left Paddle 1 | | |
| 15 | Right Paddle 1 | | |

## Feature Report (ID 0x02, 24 bytes)

Sent as an input report in response to a feature request command (output report with command 0x02), or automatically when the connected device changes.

| Offset | Size | Field | Notes |
|--------|------|-------|-------|
| 0-1 | 2 | Protocol version | uint16 LE (currently 0x0100 = v1.0) |
| 2 | 1 | Capability flags 1 | bit 0: rumble, bit 1: player LED, bit 2: accel, bit 3: gyro |
| 3 | 1 | Capability flags 2 | bit 1: RGB LED |
| 4 | 1 | Gamepad type | See enum below |
| 5 | 1 | Face style (upper 3 bits) / sub-product (lower 5 bits) | See enum below |
| 6-7 | 2 | Polling rate | Microseconds, uint16 LE (e.g., 8000 = 125 Hz) |
| 8-9 | 2 | Accel range | uint16 LE, +/- G (0 = not supported) |
| 10-11 | 2 | Gyro range | uint16 LE, +/- dps (0 = not supported) |
| 12-15 | 4 | Button usage masks | 1 byte per button byte; bits indicate active buttons |
| 16 | 1 | Touchpad count | Number of touchpads (0-2) |
| 17 | 1 | Touchpad finger count | Max simultaneous fingers |
| 18-23 | 6 | Serial / MAC | Device identifier (from board unique ID) |

### Gamepad Type Enum (byte 4)

| Value | Type |
|-------|------|
| 0 | Unknown |
| 1 | Standard |
| 2 | Xbox 360 |
| 3 | Xbox One |
| 4 | PS3 |
| 5 | PS4 |
| 6 | PS5 |
| 7 | Switch Pro |
| 8 | Joy-Con L |
| 9 | Joy-Con R |
| 10 | Joy-Con Pair |
| 11 | GameCube |
| 12 | N64 |
| 13 | SNES |

### Face Style Enum (byte 5, upper 3 bits)

| Value | Style | Layout |
|-------|-------|--------|
| 0 | Unknown | -- |
| 1 | Xbox | ABXY |
| 2 | Nintendo | BAYX |
| 3 | Sony | Cross/Circle/Square/Triangle |
| 4 | GameCube | AXBY |

## Output Report (ID 0x03, 48 bytes)

| Offset | Size | Field |
|--------|------|-------|
| 0 | 1 | Command type |
| 1-46 | 46 | Command data |

### Command Types

| Command | ID | Data Format |
|---------|----|-------------|
| Haptic | 0x01 | type(1), left_amplitude(1), left_brake(1), right_amplitude(1), right_brake(1). Type=2 for ERM simulation. |
| Feature Request | 0x02 | No data. Triggers a feature report (ID 0x02) response. |
| Player LED | 0x03 | player_number(1). Values 1-4. |
| RGB LED | 0x04 | red(1), green(1), blue(1). Each 0-255. |

## Composite Device

The SInput device exposes three HID interfaces:

1. **Gamepad** -- SInput report descriptor (reports 0x01, 0x02, 0x03)
2. **Keyboard** -- Standard 6-key rollover (modifiers + 6 keycodes, LED output for lock keys)
3. **Mouse** -- 5-button with X/Y movement, vertical wheel, horizontal pan
