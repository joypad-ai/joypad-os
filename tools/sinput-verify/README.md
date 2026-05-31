# sinput-verify

A tiny standalone SDL3 test program for sanity-checking the Joypad OS
**SInput** USB gamepad firmware (VID `2E8A`, PID `10C6`). It enumerates every
joystick SDL3 sees, opens the matching device, prints expected vs. actual
capabilities (PASS/FAIL), and then drops you into a live event tail with
hotkeys for triggering rumble, RGB LED, and player-index output reports.

This lives under `tools/` and **does not** participate in the main firmware
build. It only depends on SDL3.

## Install SDL3

macOS:

```sh
brew install sdl3
```

(Anything `>= 3.2` is recommended — that's where the SDL HIDAPI **SInput**
driver landed. Older SDL3 builds will still work, but the device will be
reported as `STANDARD` / `UNKNOWN` rather than getting the named SInput
fast-path. Buttons and axes still work via the generic HID driver — that's
still a useful signal that the firmware descriptor is correct.)

Linux: install your distro's SDL3 dev package (e.g. `libsdl3-dev`) or build
from source.

## Build

```sh
cd tools/sinput-verify
make
```

The `Makefile` discovers SDL3 via `pkg-config`. On macOS it pre-pends
`$(brew --prefix)/lib/pkgconfig` to `PKG_CONFIG_PATH` so a stock
`brew install sdl3` works without extra env tweaking.

## Run

Plug the firmware in first, then:

```sh
./sinput-verify
```

Interactive hotkeys (typed on stdin while the event loop is running):

| key | action |
|-----|--------|
| `r` | rumble 500 ms at full amplitude (`SDL_RumbleGamepad`) |
| `l` | set RGB LED red (`SDL_SetGamepadLED 255,0,0`) |
| `L` | set RGB LED green |
| `b` | set RGB LED blue |
| `o` | set RGB LED off |
| `p` | cycle player index 0..3 (`SDL_SetGamepadPlayerIndex`) |
| `?` | reprint help |
| `q` | quit (Ctrl-C also works) |

## What the output means

The startup banner enumerates every joystick SDL sees and prints its
`name`, `VID:PID`, joystick type, gamepad type, mapping string, and GUID.
Then it filters to `2E8A:10C6`, opens it as both a joystick and a gamepad,
and runs a PASS/FAIL sweep against what the firmware is supposed to expose.

Sample expected output (against the joypad-os SInput firmware):

```
sinput-verify — SDL3 3.2.x

2 joystick(s) enumerated by SDL3
  joystick id=1
    name       : Apple Internal Keyboard
    vid:pid    : 05AC:027E
    ...
  joystick id=2
    name       : Joypad OS SInput
    path       : IOService:/.../IOUSBHostHIDDevice@01100000
    vid:pid    : 2E8A:10C6
    guid       : 030000008a2e0000c610000000000000
    joy type   : GAMEPAD (1)
    is gamepad : yes
    pad name   : Joypad OS SInput
    pad type   : STANDARD (1)
    mapping    : 030000008a2e0000c610000000000000,Joypad OS SInput,...

opening 2E8A:10C6 (joystick id 2)

verifying SInput surface
  [PASS] buttons >= 32              got 32
  [PASS] axes >= 6 (LX/LY/RX/RY/LT/RT) got 6 axes, 0 hats
  [PASS] gamepad type               STANDARD (1) — STANDARD/UNKNOWN both OK
  [PASS] accel sensor               present
  [PASS] gyro sensor                present
  [PASS] touchpad count             0 touchpad(s)
  [PASS] rumble capability          yes
  [PASS] rgb led capability         yes
  [PASS] player led capability      yes
  [PASS] serial readable            JOY-...

interactive keys (stdin)
  r  rumble 500ms @ full
  l  set RGB LED red
  ...

[123456789ns] btn a  state=1
[123456790ns] btn a  state=0
[123456791ns] axis LX  value=  4321
```

A `FAIL` on `buttons >= 32` usually means SDL3 fell back to the generic HID
driver and is only mapping the W3C-standard 17 — that's the canary that the
SInput HIDAPI driver in SDL3 didn't claim the device (check SDL version, or
that `SDL_HINT_JOYSTICK_HIDAPI_SINPUT` isn't disabled in your env).

A `FAIL` on `accel sensor` / `gyro sensor` is expected on older SDL3 builds
that route SInput through `STANDARD` without sensor support — not a firmware
bug.

## License

Same as the rest of joypad-os.
