# libjoypad

Universal C library for USB and Bluetooth game controllers.

Status: **scaffolding in progress**. See [`.dev/docs/libjoypad.md`](../../../.dev/docs/libjoypad.md)
for architecture, scope, naming, and migration strategy.

## Boundary

libjoypad has **no upward dependencies** on the rest of joypad-os. It only includes:

- `<stdint.h>`, `<stdbool.h>`, `<string.h>` from the C standard library
- Its own headers under `include/joypad/`

A stray `#include "pico/stdlib.h"`, `"tusb.h"`, or any other firmware/platform reference
must fail to compile. The CMake target is configured with no firmware include paths.

## Layout

```
include/joypad/      Public contract (input_event_t, caps, layouts, feedback)
src/devices/         Per-controller parsers + feedback builders
  usb/hid/<vendor>/
  usb/xinput/
  bt/hid/<vendor>/
  bt/gip/
  bt/classic/
examples/            Reference consumers for each target
  minimal-c/         ANSI C + HIDAPI command-line dumper
  unreal/            Bare UE5 plugin
  unity/             Bare Unity native plugin + C# wrapper
  web/               WebHID + WASM single-page demo
tests/               Fixture-based replay tests
```

## Consumers

- **joypad-os** (this repo's firmware) — links libjoypad for USB/BT input parsing
- **joypad-web** — WASM build + JS shim over WebHID/Web Bluetooth (future repo)
- **joypad-unreal** — UE plugin, registers as IInputDevice (future repo)
- **joypad-unity** — native plugin + C# P/Invoke (future repo)
