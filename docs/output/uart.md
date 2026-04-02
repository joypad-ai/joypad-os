# UART Output Interface

Sends controller input events over UART serial to an external microcontroller. Designed for bridging USB/BT controller inputs to devices like ESP32-S3 (for AI processing), another Joypad board, or any external MCU that needs controller state.

## Protocol

- **Wire protocol**: UART serial with framed binary packets
- **PIO programs**: None (uses hardware UART peripheral)
- **Core**: Runs on Core 0 (not timing-critical)
- **Default baud rate**: 115200
- **Packet format**: Type byte + payload + length framing (defined in `core/uart/uart_protocol.h`)

The UART device and host (`uart_host`) can share the same UART peripheral for bidirectional communication -- controller events go out, feedback commands (rumble, LEDs) come in.

### GPIO Pins

| Signal | GPIO | Notes |
|--------|------|-------|
| TX | GP4 | Default (Qwiic cable SDA position) |
| RX | GP5 | Default (Qwiic cable SCL position) |

Pins are configurable at init time via `uart_device_init_pins()`.

## Player Support

- **Max players**: 1 (single stream of merged input events)

## Operating Modes

| Mode | Constant | Description |
|------|----------|-------------|
| Off | `UART_DEVICE_MODE_OFF` | UART device disabled |
| Stream | `UART_DEVICE_MODE_STREAM` | Send all input events continuously |
| On Change | `UART_DEVICE_MODE_ON_CHANGE` | Only send when state changes |
| On Request | `UART_DEVICE_MODE_ON_REQUEST` | Only send when remote requests |

## Button Mapping

Input events are transmitted in the universal `input_event_t` format. No console-specific button translation is performed -- the remote device receives `JP_BUTTON_*` constants directly and performs its own mapping.

## Analog Mapping

All 8 analog axes from `input_event_t` are transmitted as-is (0-255, 128 = center).

## Feedback

The UART device supports bidirectional feedback from the remote MCU:

- **Rumble**: Remote can send rumble commands (player index, left/right motor intensity, duration). Delivered via registered callback (`uart_device_set_rumble_callback`).
- **LED**: Remote can send LED commands (player index, pattern, RGB color). Delivered via registered callback (`uart_device_set_led_callback`).

### Connection Events

The UART device sends player connect/disconnect notifications:
- `uart_device_send_connect()` -- player connected with device type, VID/PID
- `uart_device_send_disconnect()` -- player disconnected

## Diagnostics

Statistics available via API:
- TX/RX packet counts
- Error count
- Queue drop count (events dropped when TX queue full)
- Connection status (true if valid packet received recently)

## Profiles

No profiles. Button mapping is passed through unchanged.

## Apps Using This Output

| App | Description |
|-----|-------------|
| `usb2uart` | USB controllers to UART serial bridge |
