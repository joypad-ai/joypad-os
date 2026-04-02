# WiFi JOCP Input Interface

Receives controller input over WiFi using the Joypad Open Controller Protocol (JOCP). The adapter runs as a WiFi access point on Pico W, and controllers (phones, other devices) connect and stream input packets over UDP.

## Protocol

- **Transport**: WiFi AP mode via CYW43 + LWIP
- **Input channel**: UDP port 30100 (low-latency, unidirectional)
- **Control channel**: TCP port 30101 (capabilities, feedback commands)
- **Packet format**: 12-byte header + 64-byte payload = 76 bytes per INPUT packet
- **Location**: `src/wifi/jocp/`

### Packet Structure

**Header** (12 bytes, little-endian):

| Field | Size | Description |
|-------|------|-------------|
| magic | 2 | `0x4A50` ("JP") |
| version | 1 | Protocol version (0x01) |
| msg_type | 1 | Message type (0x01 = INPUT) |
| seq | 2 | Sequence number |
| flags | 2 | Flags (HAS_IMU, HAS_TOUCH, KEYFRAME, ACK_REQ) |
| timestamp_us | 4 | Controller monotonic timestamp |

**Input payload** (64 bytes):

| Field | Size | Description |
|-------|------|-------------|
| buttons | 4 | 32-bit button mask (JOCP_BTN_* constants) |
| lx, ly, rx, ry | 8 | Analog sticks (signed 16-bit, centered at 0) |
| lt, rt | 4 | Triggers (unsigned 16-bit, 0-65535) |
| accel_x/y/z | 6 | Accelerometer (if HAS_IMU flag set) |
| gyro_x/y/z | 6 | Gyroscope (if HAS_IMU flag set) |
| imu_timestamp | 4 | IMU timing |
| touch[2] | 12 | Touchpad contacts (if HAS_TOUCH flag set) |
| battery_level | 1 | 0-100% |
| plug_status | 1 | Charging/wired flags |
| controller_id | 1 | 0-3 for multi-controller |
| reserved | 17 | Future use |

### Message Types

| Type | Direction | Transport | Description |
|------|-----------|-----------|-------------|
| INPUT (0x01) | Controller to Dongle | UDP | Streamed input data |
| CAPS_REQ (0x02) | Dongle to Controller | TCP | Request capabilities |
| CAPS_RES (0x03) | Controller to Dongle | TCP | Capabilities response |
| OUTPUT_CMD (0x04) | Dongle to Controller | TCP | Rumble, LED, poll rate commands |
| TIME_SYNC (0x05) | Both | TCP | Timestamp synchronization |

### Output Commands (TCP)

| Command | Description |
|---------|-------------|
| RUMBLE (0x01) | Left/right motor amplitude + duration |
| PLAYER_LED (0x02) | Player index (1-4, 0=off) |
| RGB_LED (0x03) | RGB color values |
| POLL_RATE (0x04) | Requested poll rate |

## WiFi AP Configuration

The adapter creates a WiFi access point with:

- SSID prefix configurable (appends unique suffix)
- WPA2 password protection
- Built-in DHCP server for client IP assignment
- Pairing mode: SSID broadcast can be toggled (hidden when pairing disabled)
- Configurable channel (1-11) and max connections

## Button Mapping

JOCP button bits are converted to JP_BUTTON_* constants during packet processing in `jocp_input.c`:

| JOCP Button | JP_BUTTON_* |
|-------------|-------------|
| SOUTH | B1 |
| EAST | B2 |
| WEST | B3 |
| NORTH | B4 |
| L1 | L1 |
| R1 | R1 |
| L2 | L2 |
| R2 | R2 |
| BACK | S1 |
| START | S2 |
| L3 | L3 |
| R3 | R3 |
| DU/DD/DL/DR | DU/DD/DL/DR |
| GUIDE | A1 |
| CAPTURE | A2 |

## Analog Axes

JOCP uses signed 16-bit sticks (-32768 to 32767) and unsigned 16-bit triggers (0-65535). These are normalized to the standard 0-255 range during conversion:

- Sticks: scaled from signed 16-bit to 0-255, center 128
- Triggers: scaled from 0-65535 to 0-255

## Feedback

JOCP supports bidirectional feedback over the TCP control channel:

- Rumble with amplitude and duration
- Player LED index
- RGB LED color
- Poll rate adjustment

## Configuration

- **Platform**: Pico W / Pico 2 W only (requires CYW43 WiFi)
- **UDP port**: 30100 (configurable)
- **TCP port**: 30101 (configurable)
- **Max controllers**: Up to 4 simultaneous via `controller_id`

## Apps Using This Input

- [wifi2usb](../apps/wifi2usb.md) -- WiFi controllers to USB HID output
