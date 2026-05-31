# joypad-live

Local realtime control + telemetry bridge for [Joypad OS](https://github.com/joypad-ai/joypad-os) adapters.

Speaks the device's binary USB-CDC protocol on one side, a plain JSON REST API
on the other. Designed for anything that wants to interact with a connected
adapter from any language: stream-overlay tools (Streamer.bot, OBS scripts),
viewer-interaction bots, accessibility apps, automation, telemetry pipelines.

```
your app / bot / overlay  ──HTTP──▶  joypad-live  ──CDC──▶  Joypad OS adapter
```

The full design + protocol docs are in [`streamer-live-remap.md`](./streamer-live-remap.md).

## Two reference implementations, same contract

Pick whichever fits your stack — they're drop-in compatible. Every response is
byte-identical (verified by `parity_test.sh`).

| | Path | When to pick it |
|---|---|---|
| **Python** | [`python/`](python/) | pyserial + `http.server`. Zero build step, fastest to hack on, what we use for our own validation on macOS/Linux. |
| **C#** | [`csharp/`](csharp/) | .NET 8+, `System.IO.Ports` + `HttpListener`. Matches the streamer's Streamer.bot stack on Windows. |

Both expose the same endpoints on `http://127.0.0.1:8777`:

| Method | Path | Effect |
|--------|------|--------|
| `GET`  | `/health`        | liveness + connected port |
| `GET`  | `/info`          | device app/version/board/serial |
| `GET`  | `/profiles`      | `PROFILE.LIST` |
| `POST` | `/profile/<n>`   | `PROFILE.SET {index:n}` — switch active profile |
| `POST` | `/neutral`       | `PROFILE.SET {index:0}` — reset to default |
| `POST` | `/save`          | `PROFILE.SAVE` — body forwarded as args (up to 4 persisted slots) |
| `POST` | `/apply`         | `PROFILE.APPLY` — **ephemeral RAM-only remap**, no flash write, no slot ceiling |
| `POST` | `/clear`         | `PROFILE.CLEAR` — drop the ephemeral override |

## Quickstart

```sh
# Python
pip install pyserial
python3 tools/joypad-live/python/server.py /dev/cu.usbmodemXXXX --define-demo
python3 tools/joypad-live/python/server.py /dev/cu.usbmodemXXXX

# C#
cd tools/joypad-live/csharp
dotnet run -- /dev/cu.usbmodemXXXX --define-demo
dotnet run -- /dev/cu.usbmodemXXXX
```

```sh
# Drive it:
curl http://127.0.0.1:8777/profiles
curl -X POST -d '' http://127.0.0.1:8777/profile/1
curl -X POST -d '' http://127.0.0.1:8777/neutral

# Push an arbitrary live remap (no flash write, no slot used):
curl -X POST -H "Content-Type: application/json" \
  -d '{"name":"chaos","button_map":[2,1,4,3,0,0,0,0,0,0,0,0,15,16,14,13,0,0]}' \
  http://127.0.0.1:8777/apply
curl -X POST -d '' http://127.0.0.1:8777/clear
```

## Companion tools

- [`parity_test.sh`](parity_test.sh) — runs the same endpoint sequence against both
  bridges and diffs the JSON. Re-run any time either implementation or the wire
  protocol changes.
- [`simulate_crowd_control.py`](simulate_crowd_control.py) — random-redeem
  queue/timer simulator. Drives the HTTP API the way Streamer.bot would.

## Gotchas you'll hit if you skip the docs

- The **control channel is a CDC virtual COM port**, not the HID device. C#
  apps need `System.IO.Ports.SerialPort`, not an HID library. The HID interface
  is what the *game* reads — not us.
- Only one process can own the COM port. Close `config.joypad.ai` (browser
  Web Serial) before running the bridge.
- C#: `System.IO.Ports` doesn't assert DTR/RTS on open; TinyUSB-CDC on RP2040
  won't reply without it. The C# bridge sets both at construction.
- C#: `HttpListener` on macOS returns `411 Length Required` for POSTs without
  `Content-Length`. Use `curl -X POST -d ''` for manual tests. Streamer.bot's
  Fetch URL action sets it correctly automatically.
