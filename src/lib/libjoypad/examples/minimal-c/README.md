# minimal-c

Smallest possible libjoypad consumer. Plain ANSI C + HIDAPI command-line tool.

## What it does

1. Enumerates connected HID devices via HIDAPI
2. Finds the first Sony DualSense / Victrix Pro FS for PS5 (using
   `joypad_is_sony_ds5` so the VID/PID list stays in one place)
3. Prints capabilities (`joypad_caps_t`)
4. Sends a startup feedback report (gentle rumble + blue lightbar + player-1 LED)
5. Reads HID input reports in a loop and prints the decoded `input_event_t`

## Build

```bash
cd src/lib/libjoypad/examples/minimal-c
cmake -B build
cmake --build build
./build/joypad_minimal_c
```

Requires HIDAPI:
- **macOS:** `brew install hidapi`
- **Debian/Ubuntu:** `apt install libhidapi-dev`
- **Windows:** `vcpkg install hidapi` (or download upstream and set HIDAPI_INCLUDE_DIR / HIDAPI_LIBRARY)

## Usage

```
./build/joypad_minimal_c           # stream all events
./build/joypad_minimal_c --quiet   # only print when state changes
```

`Ctrl-C` to stop.

## What it proves

libjoypad compiles and runs *standalone outside firmware*, with no platform
abstraction layer between it and the operating system. Same parser source code
as the firmware uses; same `input_event_t` structure; same feedback API. If
this binary reads a DualSense correctly, the parser is portable.
