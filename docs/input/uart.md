# UART Input Interface

Receives controller inputs from a remote device over UART serial and submits them to the router. This enables external microcontrollers -- an ESP32 Bluetooth bridge, another Joypad board, or an AI platform -- to inject controller data into the Joypad OS pipeline.

## Protocol

- **Bus**: UART serial (configurable baud rate, default varies by use case)
- **Method**: Packet-based protocol (`src/core/uart/uart_protocol.h`) with CRC validation
- **Polling**: `uart_host_task()` called from main loop, processes buffered RX data
- **Location**: `src/native/host/uart/`

The UART host and device (`uart_device`) can share the same UART peripheral for bidirectional communication: inputs arrive via UART host, feedback goes out via UART device.

## Operating Modes

| Mode | Description |
|------|-------------|
| `UART_HOST_MODE_OFF` | UART host disabled |
| `UART_HOST_MODE_NORMAL` | Submit UART inputs to router (like USB or native inputs) |
| `UART_HOST_MODE_AI_BLEND` | Blend UART inputs with existing player inputs (for AI assist) |

In AI Blend mode, the router can query `uart_host_get_injection()` to get AI-generated inputs and blend them with human player inputs according to the configured blend mode.

## Use Cases

- **ESP32 Bluetooth Bridge** (`usb2uart` app): RP2040 sends USB controller data over UART to an ESP32-S3, which re-transmits via Bluetooth
- **Controller Linking**: Two Joypad boards share inputs over UART via QWIIC cable (used in custom [GPIO controllers](gpio.md))
- **AI Input Injection**: External AI platform sends controller commands over UART

## Configuration

Default UART pins (overridable per-app):

| Pin | Default GPIO | Function |
|-----|-------------|----------|
| UART_HOST_TX_PIN | 4 | TX (to remote RX) |
| UART_HOST_RX_PIN | 5 | RX (from remote TX) |

Custom pin initialization via `uart_host_init_pins(tx, rx, baud)`.

- **Peripheral**: `uart1` by default
- **Max players**: 8 simultaneous via UART
- **Device address range**: 0xD0+ (native input range, when used for controller data)

## Connection Detection

`uart_host_is_connected()` returns true when valid packets have been received recently. Statistics are available via `uart_host_get_rx_count()`, `uart_host_get_error_count()`, and `uart_host_get_crc_errors()`.

## Feedback

The UART device side (`uart_device`) sends feedback (rumble, profile changes, output mode changes) back to the remote device over the same UART link. Callbacks can be registered for profile change and output mode change requests from the remote.

## Apps Using This Input

- [usb2uart](../apps/usb2uart.md) -- USB to UART bridge (sends to ESP32)
- [controller](../apps/controllers.md) -- Custom controllers with UART link between boards
