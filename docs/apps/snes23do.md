# snes23do

SNES/NES controller to 3DO console.

## Overview

Reads a native SNES or NES controller via GPIO shift register and outputs to a 3DO console via the PBUS protocol. Supports SNES mouse and profile switching. A cross-console bridge -- no USB involved on either side.

## Input

[SNES Input](../input/snes.md) -- GPIO-based shift register polling. Pins: CLK (GPIO 2), LATCH (GPIO 3), DATA (GPIO 4), DATA1 (GPIO 5), IOBIT (GPIO 6).

## Output

[3DO Output](../output/3do.md) -- PBUS serial PIO protocol.

## Core Configuration

| Setting | Value |
|---------|-------|
| Routing mode | SIMPLE (1:1) |
| Player slots | 1 (fixed) |
| Profile system | Yes |
| Mouse support | SNES mouse |

## Key Features

- **Multi-device** -- SNES controller, NES controller, SNES mouse all supported.
- **Profile switching** -- Runtime button mapping profiles.

## Supported Boards

| Board | Build Command |
|-------|---------------|
| RP2040-Zero | `make snes23do_rp2040zero` |

## Build and Flash

```bash
make snes23do_rp2040zero
make flash-snes23do_rp2040zero
```
