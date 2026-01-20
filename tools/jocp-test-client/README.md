# JOCP Test Client

A browser-based test client for the Joypad Open Controller Protocol (JOCP). Uses Node.js as a UDP relay since browsers cannot send UDP packets directly.

## Architecture

```
Browser (Gamepad API / UI) ──WebSocket──> Node.js Server ──UDP──> Joypad Dongle
```

## Quick Start

1. **Flash wifi2usb firmware to Pico W**
   ```bash
   cd /path/to/joypad-core
   make wifi2usb_pico_w
   make flash-wifi2usb_pico_w
   ```

2. **Connect your laptop to the dongle's WiFi**
   - SSID: `JOYPAD-XXXX` (where XXXX is a board ID suffix)
   - Password: `joypad1234`

3. **Run the test client**
   ```bash
   cd tools/jocp-test-client
   npm install
   npm start
   ```

4. **Open browser**
   - Navigate to http://localhost:3000
   - Use a physical gamepad or the on-screen buttons

## Configuration

Environment variables:

| Variable | Default | Description |
|----------|---------|-------------|
| `DONGLE_IP` | `192.168.4.1` | Dongle IP address |
| `DONGLE_PORT` | `30100` | UDP port for INPUT packets |
| `HTTP_PORT` | `3000` | Local HTTP server port |
| `POLL_RATE` | `125` | Packets per second |

Example:
```bash
DONGLE_IP=192.168.4.1 POLL_RATE=250 npm start
```

## Features

### Physical Gamepad Support
- Automatically detects connected gamepads via the Web Gamepad API
- Maps standard gamepad buttons to JOCP format
- Supports analog sticks and triggers

### On-Screen Controls
- Virtual D-pad and face buttons (tap/click)
- Draggable virtual analog sticks
- Trigger sliders

### Real-time Feedback
- Shows current button state
- Displays analog values
- Debug log for troubleshooting

## JOCP Packet Format

The client sends 76-byte JOCP INPUT packets:

| Offset | Size | Field |
|--------|------|-------|
| 0 | 2 | Magic (0x4A50 = "JP") |
| 2 | 1 | Version (0x01) |
| 3 | 1 | Message type (0x01 = INPUT) |
| 4 | 2 | Sequence number |
| 6 | 2 | Flags |
| 8 | 4 | Timestamp (microseconds) |
| 12 | 4 | Buttons (32-bit mask) |
| 16 | 2 | Left stick X (int16) |
| 18 | 2 | Left stick Y (int16) |
| 20 | 2 | Right stick X (int16) |
| 22 | 2 | Right stick Y (int16) |
| 24 | 2 | Left trigger (uint16) |
| 26 | 2 | Right trigger (uint16) |
| 28 | 48 | IMU, touch, battery, reserved |

## Troubleshooting

### "UDP send error"
- Check that your laptop is connected to the dongle's WiFi
- Verify the dongle IP (default: 192.168.4.1)
- Ensure the dongle is powered and running wifi2usb firmware

### No gamepad detected
- Press a button on the gamepad to wake it up
- Some browsers require HTTPS or localhost for Gamepad API
- Try Chrome or Edge (best Gamepad API support)

### High latency
- Increase POLL_RATE (up to 1000)
- Move closer to the dongle
- Check for WiFi interference

## Protocol Reference

See `.dev/docs/jocp.md` for full protocol specification.
