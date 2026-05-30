# web example

Single-page HTML demo. Emscripten builds libjoypad to WebAssembly; a JS shim
uses WebHID to receive raw HID reports, passes them through WASM to libjoypad's
DS5 parser, and reads back the decoded `input_event_t` for display.

Same parser source as joypad-os firmware. Compiled to ~30 KB WASM (gzipped).

## Build

Requires the Emscripten SDK:

```bash
git clone https://github.com/emscripten-core/emsdk
cd emsdk && ./emsdk install latest && ./emsdk activate latest
source ./emsdk_env.sh
```

Then from this directory:

```bash
./build.sh
```

Outputs `dist/libjoypad.js` (ES module loader) and `dist/libjoypad.wasm`.

## Run

Serve this directory with any static file server. WebHID requires a secure
context, but `localhost` is treated as secure.

```bash
python3 -m http.server 8000
# or:
npx http-server -p 8000
```

Open `http://localhost:8000/` in **Chrome or Edge**. Firefox and Safari do not
implement WebHID — see the open question in `.dev/docs/libjoypad.md`.

Click **Connect controller**, pick a Sony DualSense, and:
- Sticks, triggers, and buttons update live
- Touchpad coordinates and battery level surface in real time
- Gyro and accelerometer raw values stream
- Lightbar color picker, rumble sliders, and player-LED index send back
  through libjoypad's feedback builder via `device.sendReport()`

## What this proves

The exact same C source that runs on RP2040 hardware in joypad-os firmware
also runs in a Chrome tab without modification. WebHID provides bytes;
libjoypad-as-WASM produces `input_event_t`; JavaScript reads the fields and
draws them. No platform-specific parsing in JavaScript anywhere.

For features Gamepad API can't reach (touchpad, gyro, adaptive triggers,
lightbar), WebHID + libjoypad is the only browser path.
