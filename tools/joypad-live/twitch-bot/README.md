# joypad-live Twitch chat bot

Translates viewer `!chaos`-style chat commands into REST calls on the local
[`joypad-live`](../) bridge so viewers can mess with the streamer's controller
input in real time. Effects auto-reset after a timer so nothing gets stuck.

## Quick start

```sh
# 1. install deps
pip install -r requirements.txt

# 2. get a chat OAuth token for the account that will speak in chat
#    (often a 2nd Twitch account; your main works too)
#       https://twitchtokengenerator.com/quick/  (scopes: chat:read + chat:edit)

# 3. start the joypad-live bridge in another terminal first
python3 ../python/server.py /dev/cu.usbmodemXXXX

# 4. run the bot
TWITCH_TOKEN=oauth:abc123… TWITCH_CHANNEL=yourhandle python3 bot.py
```

The bot prints `[bot] connected as <nick>, listening on #<channel>` on success.

## Commands viewers can fire

| Command | What it does |
|---------|--------------|
| `!chaos`      | D-pad scramble (Profile #2) — 12s |
| `!aswap`      | A ↔ B (Profile #1) — 12s |
| `!invertx`    | Invert both stick X axes — 10s |
| `!invertlx`   | Invert LX only — 10s |
| `!invertrx`   | Invert RX only — 10s |
| `!inverty`    | Invert both stick Y axes — 10s |
| `!invertall`  | Invert ALL stick axes — 10s |
| `!swap`       | Swap L and R sticks — 10s |
| `!fullchaos`  | Swap + invert everything — 10s |
| `!neutral`    | Force reset (cancels any active effect) |
| `!effects`    | List all available commands in chat |

## Tunables (env vars)

| Var | Default | Effect |
|-----|---------|--------|
| `BRIDGE_URL`           | `http://127.0.0.1:8777` | Where the joypad-live bridge is listening |
| `DEFAULT_DURATION_SEC` | `12` | Length of profile-swap effects (`!chaos`, `!aswap`) |
| `OVERLAY_DURATION_SEC` | `10` | Length of overlay effects (everything else) |

## Behavior notes

- **One active effect at a time.** Firing a new command cancels the previous
  reset timer and starts a fresh one. Channel-point queueing logic — wait for
  one effect to finish before applying the next — should live upstream (in
  Streamer.bot or your own queue layer) if you want it.
- **Pre-defined profiles need to exist on the device.** `!chaos` and `!aswap`
  use `PROFILE.SELECT 2` / `PROFILE.SELECT 1`. If the device has no custom
  profiles yet, run the bridge once with `--define-demo` to install AswapB +
  Chaos before starting the bot:
  ```sh
  python3 ../python/server.py /dev/cu.usbmodemXXXX --define-demo
  ```
  Overlay effects (`!invertx` etc.) don't need any profiles — they tweak
  whatever profile is currently active.
- **The bot doesn't need Twitch Affiliate.** Channel-point redemptions are
  affiliate-only, but plain chat commands work for any account on day one.

## Customizing effects

Edit the `EFFECTS` dict in `bot.py`. Each entry maps a command name to
`(label, post-path, post-body, reset-path, duration_sec)`. Example: add a
"high-sens left stick" effect for 8 seconds:

```python
"hisens": ("high-sens LS", "/overlay", {"left_stick_sens": 175}, "/overlay/clear", 8),
```
