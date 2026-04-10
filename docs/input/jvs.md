# JVS Input Host

JVS host input driver implemented using the **[toyoshim/jvsio](https://github.com/toyoshim/jvsio)** library. Enumerates I/O boards on the JVS, parses their capability reports, and delivers digital switch and coin state each frame.

For the underlying protocol — packet structure, command set, framing, addressing — see [docs/protocols/jvs.md](../protocols/jvs.md).

## Library — jvsio (toyoshim, modified fork)

Source: [https://github.com/toyoshim/jvsio](https://github.com/toyoshim/jvsio)

jvsio is a general C library implementing the JVS v3 and JVS Dash protocols, covering both host (master) and node (slave) roles. jvs2usb uses only the host role. 

Files are located at `src/native/host/jvs/`.

### Headers

| Header | Purpose |
|--------|---------|
| `jvsio_host.h` | Host API: init, run loop, sync |
| `jvsio_client.h` | Application callbacks that the driver must implement |
| `jvsio_node.h` | Node-side API (not used in jvs2usb) |

### Host API

Called by the application each main loop iteration:

| Function | Description |
|----------|-------------|
| `JVSIO_Host_init()` | Resets the bus and begins node enumeration. Triggers enumeration callbacks for each discovered node. |
| `JVSIO_Host_run()` | Processes communication with JVS I/O until it's ready. |
| `JVSIO_Host_sync()` | Poll command to all enumerated nodes and collects switch/coin data. Must be called every loop after `run()`. |

### Client Callbacks

The driver implements these callbacks. jvsio calls them internally from `run()` and `sync()`.

#### Enumeration (called once per node, during `JVSIO_Host_init()`)

| Callback | Arguments | Description |
|----------|-----------|-------------|
| `JVSIO_Client_ioIdReceived` | `id` (ASCII string) | Board identification string: manufacturer, model, revision. |
| `JVSIO_Client_commandRevReceived` | `rev` (BCD uint8) | Command format revision. `0x13` = rev 1.3. Warns if below 1.1. |
| `JVSIO_Client_jvRevReceived` | `rev` (BCD uint8) | JVS specification revision. `0x30` = JVS 3.0. |
| `JVSIO_Client_protocolVerReceived` | `ver` (BCD uint8) | Communication version. `0x10` = ver 1.0. |
| `JVSIO_Client_functionCheckReceived` | `func[]` (4-byte entries) | Capability report. Terminated by `0x00`. See [capability codes](../protocols/jvs.md#capability-report-cmd_capabilities). |

#### Poll (called every frame from `JVSIO_Host_sync()`)

| Callback | Arguments | Description |
|----------|-----------|-------------|
| `JVSIO_Client_synced` | `players`, `coin_state`, `sw_state0[]`, `sw_state1[]` | Delivers per-player switch bytes and coin state after a successful poll. |

#### Transport (called on every transmit/receive transition)

| Callback | Description |
|----------|-------------|
| `JVSIO_Client_willSend()` | Called before transmitting. Asserts DE and RE HIGH; waits for bus to settle. |
| `JVSIO_Client_willReceive()` | Called after transmitting. Drains UART TX FIFO; releases DE and RE (receive mode). |
| `JVSIO_Client_isSenseConnected()` | Returns `true` when a node is detected on the bus. Gates enumeration. |
| `JVSIO_Client_send(data, len)` | Writes bytes to the UART TX buffer. |
| `JVSIO_Client_receive(data, len)` | Reads bytes from the UART RX buffer; returns actual byte count. |

### Sync Data

`JVSIO_Client_synced()` delivers the following per call:

- **`players`** — number of active players enumerated.
- **`coin_state`** (1 byte) — bit 7 = TEST button; bits 6–0 = coin insertion flags per slot.
- **`sw_state0[player]`** — byte 0 per player: Start, Service, D-pad (Up/Down/Left/Right), Button 1, Button 2.
- **`sw_state1[player]`** — byte 1 per player: Buttons 3–10.

Full bit layout: [docs/protocols/jvs.md — Digital Switch Data](../protocols/jvs.md#digital-switch-data-cmd_read_digital).

## Baud Rate

Configured via `JVS_COMM_SPEEDS[]`:

| Speed | Notes |
|-------|-------|
| 115,200 | Default; compatible with all JVS boards |
| 1,000,000 | High-speed; supported by most modern boards |
| 3,000,000 | Maximum; supported by select Sega and Namco boards |

The settle delay in `JVSIO_Client_willSend()` must be tuned to the active baud rate. At 115,200 baud, 200 µs is sufficient.

## Connection Detection

`JVSIO_Client_isSenseConnected()` reads two GPIO pins conditioned by the LM2903 comparator circuit on the PCB. Returns `true` when `SENSE_IN_HIGH` is LOW — indicating at least one enumerated node is pulling the sense wire. The library only sends enumeration commands when this returns `true`.
