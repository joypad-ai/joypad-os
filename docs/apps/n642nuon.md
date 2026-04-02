# n642nuon

N64 controller to Nuon DVD player.

## Overview

Reads a native N64 controller via joybus and outputs to a Nuon DVD player via the Polyface protocol. A cross-console bridge -- no USB involved on either side.

## Input

[N64 Input](../input/n64.md) -- Joybus PIO protocol on GPIO 29.

## Output

[Nuon Output](../output/nuon.md) -- Polyface PIO protocol.

## Core Configuration

| Setting | Value |
|---------|-------|
| Routing mode | SIMPLE (1:1) |
| Player slots | 1 (fixed) |
| N64 data pin | GPIO 29 |

## Supported Boards

| Board | Build Command |
|-------|---------------|
| Pico | `make n642nuon_pico` |

## Build and Flash

```bash
make n642nuon_pico
make flash-n642nuon_pico
```
