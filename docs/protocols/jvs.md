# JVS Protocol

JVS (JAMMA Video Standard) is a serial communication protocol standardized by JAMMA for arcade I/O boards. Version 3 (JVS v3) is the most widely deployed revision and the one targeted by jvs2usb.

## Physical Layer

- **Bus topology**: Daisy-chain. One host (master) with nodes (slaves).
- **Signaling**: RS-485 half-duplex differential pair (DATA+/DATA−).
- **Connector**: USB Type-B mechanical form factor. The USB differential pair carries RS-485 data, not USB protocol. The connector also carries a SENSE wire and ground via the shield.
- **Baud rate**: 115,200 bps (standard). Some boards and hosts negotiate higher speeds (1 Mbaud, 3 Mbaud) via `CMD_SET_COMMS_MODE`.


### JVS Connector Pinout (USB Type-B)

| Pin | Signal | Description |
|-----|--------|-------------|
| 1 | SENSE | Sense detection lin |
| 2 | DATA− | RS-485 B line |
| 3 | DATA+ | RS-485 A line |
| 4 | GND | Ground |
| Shell | GND | Ground (via connector shield) |

### SENSE Line

The SENSE line is used for daisy-chain detection, not data:

- When no node is connected downstream, the line is floating (open).


## Packet Structure

Every JVS packet — request or response — uses the same frame format:

```
[SYNC] [NODE] [LEN] [DATA...] [SUM]
```

| Field | Size | Description |
|-------|------|-------------|
| SYNC  | 1 B  | Always `0xE0`. Marks the start of a new packet. |
| NODE   | 1 B  | Destination address. `0xFF` = broadcast; `0x00` = host; `0x01`–`0x1F` = node. |
| LEN   | 1 B  | Contains the number of bytes left in the packet, including the SUM byte.. |
| DATA  | N B  | Command payload (host→node) or status + report bytes (node→host). |
| SUM   | 1 B  | All bytes in the packet aside from SYNC and SUM are added together, modulo 256. If this value and SUM do not match, the packet is corrupt |


## Node Addressing

Nodes have no factory-assigned address. The host assigns them during bus enumeration:

1. Host broadcasts `CMD_RESET` (`0xF0`, arg `0xD9`) to address `0xFF`. All nodes reset and clear their address.
2. Host sends `CMD_ASSIGN_ADDR` (`0xF1`) to `0xFF` with a proposed address (starting at `0x01`). 
3. Host repeats step 2 with incrementing addresses until no further node responds.

Valid node address range: `0x01`–`0x1F`.

## Command Set

### Broadcast Commands (to address `0xFF`)

| Command | Code | Argument | Description |
|---------|------|----------|-------------|
| `CMD_RESET` | `0xF0` | `0xD9` (fixed) | Resets all nodes; clears assigned addresses. |
| `CMD_ASSIGN_ADDR` | `0xF1` | New address (1 B) | Assigns an address to the next unaddressed node in the chain. |
| `CMD_SET_COMMS_MODE` | `0xF2` | Mode (1 B) | Negotiates baud rate change for boards that support it. |

### Enumeration Commands (sent to a specific node after addressing)

| Command | Code | Description |
|---------|------|-------------|
| `CMD_REQUEST_ID` | `0x10` | Request the board's ASCII identification string (manufacturer, model, revision). |
| `CMD_COMMAND_VERSION` | `0x11` | Request the command format revision (BCD; e.g. `0x13` = rev 1.3). |
| `CMD_JVS_VERSION` | `0x12` | Request the JVS specification revision (BCD; e.g. `0x30` = JVS 3.0). |
| `CMD_COMMS_VERSION` | `0x13` | Request the communication version (BCD; e.g. `0x10` = ver 1.0). |
| `CMD_CAPABILITIES` | `0x14` | Request the function code list (capability report). |
| `CMD_CONVEY_ID` | `0x15` | Send host identification string to the node (optional). |

### Poll Commands (sent each frame)

| Command | Code | Arguments | Description |
|---------|------|-----------|-------------|
| `CMD_READ_DIGITAL` | `0x20` | Players (1 B), Bytes/player (1 B) | Read digital switch state. |
| `CMD_READ_COINS` | `0x21` | Slots (1 B) | Read coin counters. |
| `CMD_READ_ANALOG` | `0x22` | Channels (1 B) | Read analog input channels (potentiometers, pedals, wheels). |
| `CMD_READ_ROTARY` | `0x23` | Channels (1 B) | Read rotary encoder values. |
| `CMD_READ_KEYPAD` | `0x24` | — | Read keypad matrix state. |
| `CMD_READ_LIGHTGUN` | `0x25` | Channels (1 B) | Read light gun screen position. |
| `CMD_READ_GPI` | `0x26` | Bytes (1 B) | Read general-purpose inputs. |

### Output Commands

| Command | Code | Arguments | Description |
|---------|------|-----------|-------------|
| `CMD_DECREASE_COIN` | `0x30` | Slot (1 B), Count (2 B) | Decrement a coin counter (credit consumed). |
| `CMD_WRITE_DIGITAL` | `0x32` | Port (1 B), Value (1 B) | Set output state (lamps, solenoids, coin lockout). |

## Response Format

Nodes respond to every addressed command with a packet directed to address `0x00` (the host):

```
[SYNC=0xE0] [NODE=0x00] [LEN] [STATUS] [DATA...] [SUM]
```

**STATUS byte:**

| Value | Meaning |
|-------|---------|
| `0x01` | Normal (OK) |
| `0x02` | Unknown command |
| `0x03` | Checksum error |
| `0x04` | Overflow |

When multiple commands are batched in a single request, the response contains one REPORT byte + data block per command, in order:

**REPORT byte:**

| Value | Meaning |
|-------|---------|
| `0x01` | Good reply — data follows |
| `0x02` | Command unsupported |
| `0x03` | Busy |

## Capability Report (`CMD_CAPABILITIES`)

The function code list is a sequence of 4-byte entries `[code, P1, P2, P3]`, terminated by a `0x00` code byte:

| Code | Feature | P1 | P2 | P3 |
|------|---------|----|----|-----|
| `0x01` | Digital switch input | Player count | Buttons per player | — |
| `0x02` | Coin input | Slot count | — | — |
| `0x03` | Analog input | Channel count | Bit resolution | — |
| `0x04` | Rotary input | Channel count | — | — |
| `0x05` | Keycode input | — | — | — |
| `0x06` | Screen position (light gun) | X bits | Y bits | Channel count |
| `0x07` | Misc switch input | Switch mask P1 | Switch mask P2 | — |
| `0x10` | Card system | Slot count | — | — |
| `0x11` | Medal hopper | Channel count | — | — |
| `0x12` | General-purpose output | Output count | — | — |
| `0x13` | Analog output | Channel count | — | — |
| `0x14` | Character output | Width | Height | Type |
| `0x15` | Backup data | — | — | — |
| `0x00` | End of list | — | — | — |

## Digital Switch Data (`CMD_READ_DIGITAL`)

For a standard 2-player, 10-button board (`players=2`, `bytes/player=2`), the response payload after the REPORT byte is 5 bytes: 1 system byte + 2 bytes × 2 players.

**System byte (shared, not per-player):**

| Bit | Signal |
|-----|--------|
| 7 | TEST button |
| 6–0 | Reserved |

**Player byte 0:**

| Bit | Signal |
|-----|--------|
| 7 | Start |
| 6 | Service |
| 5 | D-pad Up |
| 4 | D-pad Down |
| 3 | D-pad Left |
| 2 | D-pad Right |
| 1 | Button 1 |
| 0 | Button 2 |

**Player byte 1:**

| Bit | Signal |
|-----|--------|
| 7 | Button 3 |
| 6 | Button 4 |
| 5 | Button 5 |
| 4 | Button 6 |
| 3 | Button 7 |
| 2 | Button 8 |
| 1 | Button 9 |
| 0 | Button 10 |

## Coin Data (`CMD_READ_COINS`)

For each coin slot, the node returns 2 bytes:

| Bits | Field |
|------|-------|
| 15–14 | Condition code: `00` = normal, `01` = error, `10` = busy |
| 13–0 | Coin count (14-bit counter, increments on each insertion) |

The host tracks the previous count and detects insertions by delta. The TEST button (system byte bit 7 in digital switch data) is logically associated with coin/service handling.


## References

- **JVS specification**: JAMMA Video Standard Issue 3 (JAMMA, 1998). Mirror: [http://superusr.free.fr/arcade/JVS/JVST_VER3.pdf](http://superusr.free.fr/arcade/JVS/JVST_VER3.pdf)
- **AnalogJVSy** — [https://github.com/BigPanikMania/AnalogJVSy](https://github.com/BigPanikMania/AnalogJVSy): Open source JVS host on Teensy 2.0 (BigPanikMania). Command constants (`constants.h`) and packet handling (`JVS.h` / `JVSy.cpp`) served as protocol reference for jvs2usb. Itself based on JVSy by k4roshi and reverse engineering by roysmeding ([openjvs](https://github.com/TheOnlyJoey/openjvs)). GPL-3.0.
- **jvsio driver** — see [docs/input/jvs.md](../input/jvs.md) for the jvsio library implementation used in jvs2usb.
