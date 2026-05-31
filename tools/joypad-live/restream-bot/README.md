# joypad-live Restream chat firehose bot

Unified chat listener — viewers on **every** platform connected to your
Restream account (Twitch, X, YouTube, Kick, …) can fire joypad-live effects
with the same `!chaos`, `!press a`, `!swap`, `!neutral` commands. One bot
process, every platform's chat funnelled through.

Same `EFFECTS` table + `!press`/`!hold`/`!mash` button commands as the
[Twitch chat bot](../twitch-bot/) — only the listener layer differs.

## Setup (~5 minutes)

### 1. Create a Restream app

1. Sign in at https://developers.restream.io
2. Create a new app
   - **Redirect URI**: `http://localhost:8888/callback`
   - **Scopes**: `chat.read` (and `chat.write` if you want bot replies later)
3. Copy the **Client ID** and **Client Secret**

> If `developers.restream.io` gates app creation behind a paid tier, that's
> the constraint — Restream Chat API needs Standard+ on the current pricing.

### 2. Install + authorize (one-time)

```sh
pip install -r requirements.txt    # or use the venv at tools/joypad-live/.venv

export RESTREAM_CLIENT_ID=your_client_id
export RESTREAM_CLIENT_SECRET=your_client_secret

python3 oauth.py
```

`oauth.py` opens a browser → you click "Authorize" → tokens write to
`~/.joypad-live/restream-tokens.json` (chmod 600). Done once per machine.

### 3. Run the bot

```sh
# bridge must be running in another terminal first:
#   python3 ../python/server.py /dev/cu.usbmodemXXXX

python3 bot.py
```

You'll see:
```
[restream-bot] connected to wss://chat.api.restream.io/ws
[restream-bot] bridge at http://127.0.0.1:8777
```

Any viewer on any Restream-connected platform now fires effects by typing
`!chaos`, `!press a`, `!swap` etc. in their native chat.

## What viewers can do

Same as `tools/joypad-live/twitch-bot/`:

| Command | What it does |
|---|---|
| `!chaos` | d-pad scramble (Profile #2) — 12s |
| `!aswap` | A ↔ B (Profile #1) — 12s |
| `!invertx`, `!invertlx`, `!invertrx`, `!inverty`, `!invertall` | axis inversions — 10s |
| `!swap`, `!fullchaos` | stick swap / swap+invert — 10s |
| `!neutral` | force reset |
| `!press <btn>` | single tap (a/b/x/y/up/down/left/right/start/select/l1-r2/l3/r3/home) |
| `!hold <btn>`  | 2s hold |
| `!mash <btn>`  | rapid taps for 3s |
| `!release`     | force-release any stuck synthetic buttons |

## Tunables (env vars)

| Var | Default | Effect |
|-----|---------|--------|
| `BRIDGE_URL`           | `http://127.0.0.1:8777`            | where joypad-live's REST bridge listens |
| `DEFAULT_DURATION_SEC` | `12`                                | length of profile-swap effects |
| `OVERLAY_DURATION_SEC` | `10`                                | length of overlay effects |
| `RESTREAM_WS_URL`      | `wss://chat.api.restream.io/ws`     | override the WebSocket endpoint |
| `DEBUG`                | `0`                                 | `1` to log every raw event from the firehose (useful while wiring up) |

## Running alongside the Twitch bot

Both bots talk to the **same bridge** over HTTP. Run them in parallel and
the bridge serializes access via its internal serial lock. Twitch viewers
hit the Twitch IRC listener; Restream subscribers (Twitch + X + YT + …)
hit the Restream firehose. Either path fires the same effects.

Why you might do this: even if your Restream tier gates the API, your
Twitch chat still works via the free IRC bot. Restream just adds X / YT
viewers when you can swing the API tier.

## Debugging tips

- **`DEBUG=1 python3 bot.py`** logs every raw event from the WS — invaluable
  if Restream's protocol version differs from what `extract_message()` handles.
  If you see chat events that don't trigger commands, paste the raw JSON
  into the function and adjust the field paths.
- **Auth issues**: `rm ~/.joypad-live/restream-tokens.json` and re-run
  `python3 oauth.py` to start fresh.
- **WS keeps reconnecting**: usually means token expired and refresh failed.
  Check that your Restream app's scopes match (need `chat.read` at minimum).

## What's not here (yet)

- **Bot replies in chat** ("🎮 ffpsx → tap A!"). Posting back through
  Restream requires per-platform write tokens, not just the firehose
  `chat.read`. We could add it but it'd 2x the OAuth flow complexity.
  The Twitch IRC bot replies in chat because IRC has bidirectional auth
  built in.
- **Per-platform cooldowns / mod-only flags**. The Restream event payload
  has username + platform, so easy to filter — say the word and we add it.
- **Channel-point redemptions**. Restream doesn't bridge those — would
  need direct Twitch EventSub.
