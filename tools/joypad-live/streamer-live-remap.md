# Live Controller Remap for Stream Crowd-Control (end-to-end)

Replicate the setup LiminalityCarb is building: Twitch viewers trigger live
controller button-swap effects on a `usb2usb` adapter, with effects timed and
queued, returning to neutral when they expire.

```
Twitch ──▶ Streamer.bot ──▶ local REST API ──▶ USB-CDC ──▶ Joypad OS (usb2usb)
 redeem    queue/timer/        (C#, this doc)   PROFILE.SET   remaps live, no reboot
           on-screen FX
```

The adapter does the remapping in firmware, so it works **globally across every
game / emulator** on the PC — the OS just sees a gamepad whose buttons changed.

---

## 0. TL;DR

- The control channel is the adapter's **CDC virtual COM port**, *not* the HID
  gamepad. Use a **serial** library (`System.IO.Ports`), never an HID library.
- Predefine up to **4 custom profiles** (button maps) on the device via
  config.joypad.ai or the `PROFILE.SAVE` command.
- An effect = `PROFILE.SET {index:N}`. Return to neutral = `PROFILE.SET {index:0}`.
- All of this works on **stock `usb2usb` firmware** — no firmware changes needed.
- Queue/timer/“return after X seconds” logic lives host-side (Streamer.bot + the API).

---

## 1. The adapter exposes TWO USB interfaces

`usb2usb` in SInput mode (and HID/Switch/KB-Mouse/CDC modes) is a **composite
USB device**:

| Interface | Who reads it | What it's for |
|-----------|--------------|---------------|
| HID gamepad | the game / Steam / emulator | the actual controller input |
| **CDC virtual COM port** | **our REST API** | config + `PROFILE.*` commands |

> ⚠️ The single most common mistake: the device shows up as **USB HID** in
> Windows Device Manager, so people grab an HID library and it fails. The command
> channel is a plain **COM port** — look under **Ports (COM & LPT)** in Device
> Manager (e.g. `COM5`). In C#, that's `System.IO.Ports.SerialPort` (add the
> `System.IO.Ports` NuGet). No HID library required.

The game keeps reading the HID interface while our API owns the COM port — they
don't conflict. The only conflict is **two host programs wanting the COM port at
once**: don't leave config.joypad.ai open in a browser while the API runs (the
browser's Web Serial grabs the same port).

---

## 2. CDC wire protocol

Binary frame carrying a JSON payload. Source of truth: `src/usb/usbd/cdc/cdc_protocol.h`.
Reference clients: `tools/cdc_test.py` (Python), `tools/sinput-verify/cdcsend.c` (C).

```
[SYNC:1=0xAA][LEN:2 LE][TYPE:1][SEQ:1][PAYLOAD:LEN bytes][CRC:2 LE]
```

- `LEN` — payload length only (not header/CRC), little-endian
- `TYPE` — `0x01` CMD (host→device), `0x02` RSP (device→host), `0x03` EVT (async)
- `SEQ` — request/response correlation byte (echo it back; any value is fine)
- `PAYLOAD` — UTF-8 JSON, e.g. `{"cmd":"PROFILE.SET","index":2}`
- `CRC` — **CRC-16-CCITT** (poly `0x1021`, init `0xFFFF`) over `TYPE+SEQ+PAYLOAD`, little-endian

To issue a command: send a `TYPE=0x01` frame, then read until you see a
`TYPE=0x02` frame and decode its JSON payload.

---

## 3. The commands we use

All responses are JSON with `"ok":true` or `{"ok":false,"error":"..."}`.

### `PROFILE.LIST` — enumerate profiles
Request: `{"cmd":"PROFILE.LIST"}`
Response (usb2usb has no built-in profiles, so index 0 is a virtual Default):
```json
{"ok":true,"active":0,"profiles":[
  {"index":0,"name":"Default","builtin":true,"editable":false},
  {"index":1,"name":"AswapB","builtin":false,"editable":true},
  {"index":2,"name":"Chaos","builtin":false,"editable":true}
]}
```

### `PROFILE.SELECT` — switch active profile (RAM only, no flash write)
Request: `{"cmd":"PROFILE.SELECT","index":2}` → `{"ok":true,"index":2,"name":"Chaos","persisted":false}`

This is the hot-path verb for live control. Updates the runtime active index in
RAM only — **no flash write, no flash wear**. After reboot, the device comes
back to whatever was last persisted via `PROFILE.SET` (or the web config).

Use this for every channel-point redeem, every chaos-effect swap, every "back
to neutral." The bridges' `POST /profile/<n>` and `POST /neutral` endpoints
already map to `PROFILE.SELECT` by default.

`PROFILE.LIST.active` reflects the SELECT'd index while it's in effect, so the
bot can read "what's running right now."

### `PROFILE.SET` — switch active profile + persist as new boot default
Request: `{"cmd":"PROFILE.SET","index":2}` → `{"ok":true,"index":2,"name":"Chaos"}`

Same runtime effect as `PROFILE.SELECT` but **writes to flash immediately** so
the choice survives reboot. Use this only for deliberate config changes (the
web config UI, "make this my new default"), not for hot-loop switching —
thousands of writes per stream would burn out the chip.

Exposed in the bridges as `POST /default/<n>`.

### `PROFILE.SAVE` — create/update a custom profile
- `index:255` creates a new slot (max 4). `index:1..4` updates an existing one.
- Built-in indices are rejected (`"cannot modify built-in profile"`).

Request:
```json
{"cmd":"PROFILE.SAVE","index":255,"name":"AswapB",
 "button_map":[2,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]}
```
Response: `{"ok":true,"index":1,"name":"AswapB"}`

### `PROFILE.GET` — read one profile (incl. its button_map for custom slots)
Request: `{"cmd":"PROFILE.GET","index":1}`

### `PROFILE.APPLY` — ephemeral, RAM-only live remap (no flash write)
The crowd-control-native primitive. Push an arbitrary button_map for *this exact moment*
without touching flash, without using a slot. No 4-profile ceiling, no flash wear,
unlimited unique maps per stream.

Request:
```json
{"cmd":"PROFILE.APPLY","name":"chaos-77",
 "button_map":[2,1,4,3,0,0,0,0,0,0,0,0,15,16,14,13,0,0]}
```
Response: `{"ok":true,"ephemeral":true,"name":"chaos-77"}`

Fields besides `button_map` (all optional, default to passthrough/100% sens):
`name`, `left_stick_sens`, `right_stick_sens`, `flags`, `socd_mode`,
`l2_threshold`, `r2_threshold` — same semantics as `PROFILE.SAVE`.

The override persists until:
- `PROFILE.CLEAR` drops it explicitly,
- `PROFILE.SET` is called (explicit selection wins),
- on-device SELECT+D-pad combo cycles profiles.

`PROFILE.LIST` ignores the ephemeral — it only reports stored profiles. `active`
in the list reflects the flash-stored selection that will resume after CLEAR.

### `PROFILE.CLEAR` — drop the ephemeral override
Request: `{"cmd":"PROFILE.CLEAR"}` → `{"ok":true,"ephemeral":false}`
Idempotent. After this, the flash-stored active profile is in effect again.

### `OVERLAY.SET` — RAM-only live-tweak layer (composes on top of *any* active profile)
The crowd-control composition primitive: lets you say *"invert LX while keeping
the user's current profile fully intact."* Unlike `PROFILE.APPLY`, the overlay
does **not** replace the active button_map — it only adds stick / SOCD /
threshold transforms. Works on top of any profile: built-in `profile_t`
(`gc2usb`/`usb2nuon`/etc.), custom flash profiles, or a `PROFILE.APPLY`'d
ephemeral. Fields set to `0` are skipped, so the overlay is strictly additive.

Request:
```json
{"cmd":"OVERLAY.SET","flags":8,"left_stick_sens":120,"socd_mode":2}
```
Response: echoes the full overlay struct + `"ok":true,"overlay":true`.

Fields (all optional):
- `flags` — OR'd with profile flags (`SWAP_STICKS`, `INVERT_L/R X/Y`)
- `left_stick_sens`, `right_stick_sens` — `0` = no change; `1..200` replaces
- `socd_mode` — `0` = no change; `1..3` overrides
- `l2_threshold`, `r2_threshold` — `0` = no change; `1..255` overrides

Invisible to `PROFILE.LIST` (it's a separate composition layer, not a profile).
Persists in RAM until `OVERLAY.CLEAR` (or device power cycle).

### `OVERLAY.CLEAR` — drop the overlay
Request: `{"cmd":"OVERLAY.CLEAR"}` → `{"ok":true,"overlay":false}`
Idempotent.

### Other useful ones
`INFO` (device/app/version), `PING`, `MODE.GET/LIST`, `SETTINGS.GET`.
Full table: `src/usb/usbd/cdc/cdc_commands.c` (`commands[]`).

---

## 4. The `button_map` format

A custom profile's `button_map` is an **18-element array** (`flash.h`,
`custom_profile_t`). Array **position = source button**; **value = target**.

Positions (source):
```
 0 B1   1 B2   2 B3   3 B4     (face: A B X Y on Xbox layout)
 4 L1   5 R1   6 L2   7 R2     (shoulders / triggers)
 8 S1   9 S2  10 L3  11 R3     (select, start, stick clicks)
12 DU  13 DD  14 DL  15 DR     (d-pad up/down/left/right)
16 A1  17 A2                   (guide / aux)
```

Values (target):
- `0` = passthrough (keep original button)
- `1..24` = remap to button N (1-based: `1`=B1, `2`=B2, `3`=B3, `4`=B4, …)
- `255` (`0xFF`) = disable (button does nothing)

Examples:
- **A↔B swap:** position 0 (B1) → `2`, position 1 (B2) → `1`, rest `0`:
  `[2,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]`
- **D-pad scramble** (U→L, D→R, L→D, R→U): positions 12–15 = `15,16,14,13`
  → `[0,0,0,0,0,0,0,0,0,0,0,0,15,16,14,13,0,0]`
  (target values: DU=13, DD=14, DL=15, DR=16)
- A profile can also set stick sensitivity, swap sticks, invert axes,
  SOCD mode, and trigger thresholds — see `custom_profile_t` and the
  `PROFILE.SAVE` handler. The `flags` byte:

  | Bit | Mask  | Flag                       |
  |-----|-------|----------------------------|
  | 0   | `0x01` | `PROFILE_FLAG_SWAP_STICKS` |
  | 1   | `0x02` | `PROFILE_FLAG_INVERT_LY`   |
  | 2   | `0x04` | `PROFILE_FLAG_INVERT_RY`   |
  | 3   | `0x08` | `PROFILE_FLAG_INVERT_LX`   |
  | 4   | `0x10` | `PROFILE_FLAG_INVERT_RX`   |

  Combine with bitwise OR — e.g. `flags: 30` (0x1E) inverts both sticks
  fully (LX+LY+RX+RY) without touching the button_map.

> The 4-slot limit only applies to **persisted** profiles (`PROFILE.SAVE`).
> For unlimited live variety — e.g. every redeem generates a brand-new random
> map — use `PROFILE.APPLY` (§3): RAM only, no flash writes, no slot ceiling.

---

## 5. The local REST API

Two reference implementations live in this repo — same HTTP contract, same CDC
wire protocol — so you can pick whichever language fits your stack:

| Language | Path | When to use |
|----------|------|-------------|
| **C#** (.NET 8+) | `tools/joypad-live/csharp/` | the streamer's actual stack (matches Streamer.bot side) |
| **Python** (3.10+) | `tools/joypad-live/python/`    | quick prototyping, our macOS validation |

Both expose the same endpoints on `http://127.0.0.1:8777` and accept the same
`--define-demo` flag to install the demo profiles. The Python crowd-control
simulator (`simulate_crowd_control.py`) works against either — proved end-to-end
on `usb2usb` running on a Feather RP2040 USB host.

### Run the C# bridge

```sh
cd tools/joypad-live/csharp
dotnet run -- /dev/cu.usbmodemXXXX --define-demo    # one-time, install profiles
dotnet run -- /dev/cu.usbmodemXXXX                  # serve REST forever
# Windows: replace the port with COMx
```

**Two non-obvious gotchas baked into the C# bridge (see Program.cs):**

- **`DtrEnable = true; RtsEnable = true;`** — `System.IO.Ports` does NOT assert
  DTR/RTS on open, but TinyUSB-CDC on RP2040 needs DTR asserted before it will
  reply to commands. Without this, every command times out. pyserial does this
  by default on POSIX, which is why the Python bridge worked without it.
- **`HttpListener` on macOS/Linux returns `411 Length Required` for POSTs that
  don't carry `Content-Length` (even `Content-Length: 0`).** From `curl`, use
  `-X POST -d ''` for endpoints with no body. Streamer.bot's Fetch URL action
  sets it correctly automatically.

### Minimal C# snippet (CRC16 + frame + send/recv)

If you want to paste the protocol layer directly into an existing C# app rather
than running our bridge, this is the whole thing. Full server is in
`tools/joypad-live/csharp/Program.cs`.

```sh
dotnet new console -n JoypadLive
cd JoypadLive
dotnet add package System.IO.Ports
# replace Program.cs with the below, then: dotnet run -- COM5   (or /dev/cu.usbmodemXXXX)
```

```csharp
using System;
using System.IO.Ports;
using System.Net;
using System.Text;

class Program
{
    static SerialPort _sp = null!;
    static readonly object _lock = new(); // serialize access to the one port

    static ushort Crc16(byte[] d)
    {
        ushort crc = 0xFFFF;
        foreach (byte x in d) {
            crc ^= (ushort)(x << 8);
            for (int b = 0; b < 8; b++)
                crc = (crc & 0x8000) != 0 ? (ushort)((crc << 1) ^ 0x1021)
                                          : (ushort)(crc << 1);
        }
        return crc;
    }

    // Send one CDC command, return device JSON response (null on timeout).
    static string? Cdc(string json)
    {
        byte[] p = Encoding.ASCII.GetBytes(json);
        byte type = 0x01, seq = 0x01;

        byte[] crcIn = new byte[2 + p.Length];
        crcIn[0] = type; crcIn[1] = seq;
        Array.Copy(p, 0, crcIn, 2, p.Length);
        ushort crc = Crc16(crcIn);

        byte[] f = new byte[5 + p.Length + 2];
        int i = 0;
        f[i++] = 0xAA;
        f[i++] = (byte)(p.Length & 0xFF);
        f[i++] = (byte)((p.Length >> 8) & 0xFF);
        f[i++] = type;
        f[i++] = seq;
        Array.Copy(p, 0, f, i, p.Length); i += p.Length;
        f[i++] = (byte)(crc & 0xFF);
        f[i++] = (byte)((crc >> 8) & 0xFF);

        lock (_lock) {
            _sp.DiscardInBuffer();
            _sp.Write(f, 0, f.Length);

            byte[] buf = new byte[1024]; int got = 0;
            long deadline = Environment.TickCount64 + 800;
            while (Environment.TickCount64 < deadline && got < buf.Length) {
                try { got += _sp.Read(buf, got, buf.Length - got); }
                catch (TimeoutException) { }
                for (int q = 0; q + 5 <= got; q++) {
                    if (buf[q] != 0xAA) continue;
                    int len = buf[q + 1] | (buf[q + 2] << 8);
                    if (q + 5 + len + 2 > got) break;
                    if (buf[q + 3] == 0x02) // RSP
                        return Encoding.ASCII.GetString(buf, q + 5, len);
                }
            }
            return null;
        }
    }

    static void Main(string[] args)
    {
        string port = args.Length > 0 ? args[0] : "COM5";
        _sp = new SerialPort(port, 115200) { ReadTimeout = 100 };
        _sp.Open();
        Console.WriteLine($"Joypad CDC open on {port}: {Cdc("{\"cmd\":\"INFO\"}")}");

        var http = new HttpListener();
        http.Prefixes.Add("http://127.0.0.1:8777/");
        http.Start();
        Console.WriteLine("REST API on http://127.0.0.1:8777/  (GET /profiles, POST /profile/{n})");

        while (true) {
            var ctx = http.GetContext();
            string path = ctx.Request.Url!.AbsolutePath.Trim('/');
            string resp;
            try {
                if (path == "profiles")
                    resp = Cdc("{\"cmd\":\"PROFILE.LIST\"}") ?? "{\"ok\":false,\"error\":\"timeout\"}";
                else if (path.StartsWith("profile/") && int.TryParse(path.Substring(8), out int n))
                    resp = Cdc($"{{\"cmd\":\"PROFILE.SET\",\"index\":{n}}}") ?? "{\"ok\":false,\"error\":\"timeout\"}";
                else if (path == "neutral")
                    resp = Cdc("{\"cmd\":\"PROFILE.SET\",\"index\":0}") ?? "{\"ok\":false,\"error\":\"timeout\"}";
                else { ctx.Response.StatusCode = 404; resp = "{\"ok\":false,\"error\":\"not found\"}"; }
            } catch (Exception e) {
                ctx.Response.StatusCode = 500;
                resp = $"{{\"ok\":false,\"error\":\"{e.Message}\"}}";
            }
            byte[] outBytes = Encoding.UTF8.GetBytes(resp);
            ctx.Response.ContentType = "application/json";
            ctx.Response.OutputStream.Write(outBytes, 0, outBytes.Length);
            ctx.Response.Close();
            Console.WriteLine($"{path} -> {resp}");
        }
    }
}
```

Endpoints (both bridges, on `http://127.0.0.1:8777`):

| Method | Path | Effect | Flash? |
|--------|------|--------|--------|
| `GET`  | `/profiles`      | `PROFILE.LIST` | – |
| `POST` | `/profile/<n>`   | `PROFILE.SELECT {index:n}` | **no — RAM only** |
| `POST` | `/neutral`       | `PROFILE.SELECT {index:0}` | **no — RAM only** |
| `POST` | `/apply`         | `PROFILE.APPLY` (body forwarded) | **no — RAM only** |
| `POST` | `/clear`         | `PROFILE.CLEAR` | – |
| `POST` | `/overlay`       | `OVERLAY.SET` (body forwarded) — composes on top of active profile | **no — RAM only** |
| `POST` | `/overlay/clear` | `OVERLAY.CLEAR` | – |
| `POST` | `/default/<n>`   | `PROFILE.SET {index:n}` — writes flash, persists past reboot | **yes** |
| `POST` | `/save`          | `PROFILE.SAVE` (body forwarded) — define/update a stored profile | **yes** |

Hot-path commands are flash-free by design — a streamer firing thousands of
profile swaps per session won't wear out the chip. Only `/default` and `/save`
touch flash.

> Finding the port: hardcode `COMx` for v1. To auto-detect on Windows, filter
> serial ports by VID `0x239A` (Adafruit) via WMI (`Win32_PnPEntity`).

---

## 6. Streamer.bot wiring

1. **Twitch → Streamer.bot:** Channel Point Reward trigger (or Tiltify / file-watch
   for donation trackers).
2. **Queue + timer (Streamer.bot actions + a little C#):**
   - On redeem: if an effect is already active, enqueue; else apply now.
   - Pick a random effect index (e.g. 1–4), call the API, start a countdown.
   - On expiry: pop the queue → apply next, or call `/neutral`.
   - Drive on-screen indicators (OBS source toggles) alongside.
3. **Apply an effect:** Streamer.bot **Fetch URL / HTTP Request** sub-action →
   `POST http://127.0.0.1:8777/profile/{n}`.
4. **Return to neutral:** `POST http://127.0.0.1:8777/neutral`.
5. Export the actions + C# as a Streamer.bot **import code** to share the bundle.

Streamer.bot's built-in HTTP action is the clean integration point — no need for
it to do serial itself (its C# sandbox is limited, and serial from there is
painful). Keep all device I/O in the standalone API.

---

## 7. Testing without Twitch

Validated end-to-end on macOS without a live Twitch front end: flash a
`usb2usb` build (SInput mode default), then drive the REST API with `curl` or
the bundled `simulate_crowd_control.py`, which fires effects on a timer and
returns to neutral — proving the "effect for N seconds then neutral" loop
before any Streamer.bot wiring. `tools/cdc_test.py <port>` is handy for poking
individual `PROFILE.*` commands directly.

---

## 8. Gotchas

- **One owner per COM port.** Close config.joypad.ai (browser Web Serial) before
  running the API. Only one process can hold the port.
- **CDC, not HID** — see §1.
- **`PROFILE.SET` persists the active index to flash** (debounced). Fine for a
  stream's volume; if we ever push thousands of switches/hour, add an ephemeral
  RAM-only variant.
- **Don't block on reads** — always time out (the device only replies to valid
  framed commands; garbage in = no `0x02` frame back).

---

## 9. Ideas / not yet implemented

> These are not shipped — aspirational, listed so contributors know the
> direction.

- **Per-game detection** (console outputs) → auto-profile per game.
- Ship the bridge + Streamer.bot import code as a reusable “Joypad Crowd Control”
  bundle for other streamers.

## 10. Bonus: unlimited random scrambles with `PROFILE.APPLY`

A queue/timer pseudo-loop that fires a brand-new random map every redeem,
no flash writes:

```python
import random, requests, time
BUTTONS = list(range(0, 18))                  # 18 source positions
TARGETS = list(range(1, 19)) + [0xFF, 0]      # plus disable + passthrough

def random_map():
    return [random.choice(TARGETS) for _ in range(18)]

while True:
    requests.post("http://127.0.0.1:8777/apply",
                  json={"name": f"r{random.randint(0,999):03d}",
                        "button_map": random_map()})
    time.sleep(8)                              # effect duration
    requests.post("http://127.0.0.1:8777/clear")
    time.sleep(2)                              # neutral cool-down
```

Every redeem can be a uniquely-scrambled chaos run; the firmware doesn't even
notice — it just remaps the running input stream.
```
