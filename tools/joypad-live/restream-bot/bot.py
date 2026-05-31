#!/usr/bin/env python3
"""
Restream Chat firehose bot — joypad-live unified listener.

Listens to Restream's WebSocket chat firehose so viewers from ANY connected
platform (Twitch, X, YouTube, Kick, …) can fire effects with chat commands
like !chaos. One listener, every platform.

Architecture:

    viewer on Twitch ─┐
    viewer on X ──────┼─▶ Restream Chat firehose ──▶ this bot ──▶ joypad-live
    viewer on YT ─────┘   (WebSocket, JSON events)        bridge → device

Pre-req:
    1. Sign in at https://developers.restream.io and create an app.
       Redirect URI: http://localhost:8888/callback
    2. export RESTREAM_CLIENT_ID=… RESTREAM_CLIENT_SECRET=…
    3. python3 oauth.py        (one-time browser auth)
    4. python3 bot.py          (this script — keep running while streaming)

Tunables (env vars):
    BRIDGE_URL              http://127.0.0.1:8777
    DEFAULT_DURATION_SEC    12
    OVERLAY_DURATION_SEC    10
    RESTREAM_WS_URL         override the WebSocket URL (testing)
    DEBUG                   1 to log raw events from the firehose
"""

import asyncio
import json
import os
import sys
import time

import requests
import websockets

from oauth import get_access_token

BRIDGE_URL  = os.environ.get("BRIDGE_URL", "http://127.0.0.1:8777")
DEFAULT_DURATION = int(os.environ.get("DEFAULT_DURATION_SEC", "12"))
OVERLAY_DURATION = int(os.environ.get("OVERLAY_DURATION_SEC", "10"))
WS_URL_BASE = os.environ.get("RESTREAM_WS_URL", "wss://chat.api.restream.io/ws")
DEBUG = bool(int(os.environ.get("DEBUG", "0")))


def post(path: str, body: dict | None = None,
         user: str | None = None, platform: str | None = None) -> dict | None:
    """POST to the bridge with viewer attribution headers so the overlay's
    live feed can show 'alice@twitch → !chaos'."""
    headers = {}
    if user:
        headers["X-User"] = user
        headers["X-Platform"] = platform or "?"
    try:
        r = requests.post(f"{BRIDGE_URL}{path}", json=body, headers=headers, timeout=2)
        return r.json()
    except Exception as e:
        print(f"[bridge] POST {path} failed: {e}", flush=True)
        return None


# Same EFFECTS catalog as the Twitch bot — keep in sync if either changes.
EFFECTS = {
    "chaos":      ("chaos (d-pad scramble)", "/profile/2",  None,            "/neutral",        DEFAULT_DURATION),
    "aswap":      ("A↔B swap",               "/profile/1",  None,            "/neutral",        DEFAULT_DURATION),
    "invertlx":   ("invert LX",              "/overlay",    {"flags": 8},    "/overlay/clear",  OVERLAY_DURATION),
    "invertrx":   ("invert RX",              "/overlay",    {"flags": 16},   "/overlay/clear",  OVERLAY_DURATION),
    "invertx":    ("invert both X",          "/overlay",    {"flags": 24},   "/overlay/clear",  OVERLAY_DURATION),
    "inverty":    ("invert both Y",          "/overlay",    {"flags": 6},    "/overlay/clear",  OVERLAY_DURATION),
    "invertall":  ("invert all axes",        "/overlay",    {"flags": 30},   "/overlay/clear",  OVERLAY_DURATION),
    "swap":       ("swap sticks",            "/overlay",    {"flags": 1},    "/overlay/clear",  OVERLAY_DURATION),
    "fullchaos":  ("swap + invert all",      "/overlay",    {"flags": 31},   "/overlay/clear",  OVERLAY_DURATION),
}


# ----------------------------------------------------------------------------
# Restream event extraction
# ----------------------------------------------------------------------------
# Restream's WebSocket emits JSON events in a few shapes depending on
# protocol version. To stay resilient, we try several known fields. Run with
# DEBUG=1 to log every raw event when first wiring up; tweak this fn if your
# event shape differs from what's handled.

PLATFORM_IDS = {
    # Restream eventSourceId → human label. Observed at the payload root,
    # alongside text/userId, in reply_created events.
    2:  "twitch",
    13: "youtube",
    28: "x",
    30: "kick",
    # Older streamingPlatformId values seen in legacy event shapes:
    5:  "twitch",
}

# Restream chat-bearing actions. reply_accepted / reply_confirmed are status
# updates for messages WE'D send (writes) — read-only bot ignores them.
CHAT_ACTIONS = {"reply_created", "chat_message", "event"}


def extract_message(ev: dict) -> tuple[str | None, str | None, str | None]:
    """Return (text, username, platform) or (None,None,None) for non-chat."""
    action = ev.get("action")
    if action and action not in CHAT_ACTIONS:
        return None, None, None

    # Two known shapes in the wild:
    #   (A) current: payload.text + payload.eventSourceId + payload.userId
    #   (B) legacy:  payload.eventPayload.{text,author,connection,…}
    payload = ev.get("payload") or ev
    inner = payload.get("eventPayload") or payload

    text = inner.get("text") or inner.get("message") or inner.get("body")
    if not text:
        return None, None, None

    # User: try every name field we've seen, fall back to numeric userId.
    author = inner.get("author") or {}
    user = (
        author.get("displayName")
        or author.get("name")
        or inner.get("authorName")
        or inner.get("displayName")
        or inner.get("username")
        or (f"u{inner['userId']}" if inner.get("userId") else None)
        or "?"
    )

    # Platform: try several known fields (eventSourceId at payload root for
    # the current schema; connection.streamingPlatformId for the legacy one).
    pid = (
        inner.get("eventSourceId")
        or payload.get("eventSourceId")
        or (inner.get("connection") or {}).get("streamingPlatformId")
        or inner.get("streamingPlatformId")
    )
    platform = PLATFORM_IDS.get(pid, f"platform{pid}" if pid else "?")

    return text, user, platform


# ----------------------------------------------------------------------------
# Command dispatcher
# ----------------------------------------------------------------------------

class State:
    """Single active timed-effect, shared between Restream and Twitch bots
    if both ever run side-by-side. Each is process-local — the bridge
    serializes via its serial lock, so concurrent commands are safe even
    if multiple bots fire at once."""
    active_label: str | None = None
    active_reset_path: str | None = None
    active_reset_task: asyncio.Task | None = None


async def _reset_after(reset_path: str, secs: int, label: str):
    try:
        await asyncio.sleep(secs)
    except asyncio.CancelledError:
        return
    post(reset_path, user="system", platform="timer")
    if State.active_label == label:
        State.active_label = None
        State.active_reset_path = None
    print(f"[effect] {label} → reset", flush=True)


async def fire_effect(key: str, user: str, platform: str):
    if key not in EFFECTS:
        return
    label, path, body, reset_path, secs = EFFECTS[key]
    if State.active_reset_task and not State.active_reset_task.done():
        State.active_reset_task.cancel()
    r = post(path, body, user=user, platform=platform)
    if r is None or not r.get("ok"):
        print(f"[effect] {key} → bridge error", flush=True)
        return
    State.active_label = label
    State.active_reset_path = reset_path
    State.active_reset_task = asyncio.create_task(_reset_after(reset_path, secs, label))
    print(f"[effect] {label} for {secs}s (by {user}@{platform})", flush=True)


async def fire_press(button: str, user: str, platform: str, mode: str = "tap"):
    button = button.strip().lower()
    if not button:
        return
    if mode == "tap":
        r = post(f"/press/{button}", user=user, platform=platform)
        if r and r.get("ok"):
            print(f"[press] tap {button} (by {user}@{platform})", flush=True)
        else:
            print(f"[press] {button} rejected: {(r or {}).get('error')}", flush=True)
    elif mode == "hold":
        r = post(f"/hold/{button}", user=user, platform=platform)
        if r is None or not r.get("ok"):
            print(f"[hold] {button} rejected: {(r or {}).get('error')}", flush=True)
            return
        print(f"[hold] {button} for 2s (by {user}@{platform})", flush=True)
        await asyncio.sleep(2.0)
        post("/release", user="system", platform="timer")
    elif mode == "mash":
        probe = post(f"/press/{button}", user=user, platform=platform)
        if probe is None or not probe.get("ok"):
            print(f"[mash] {button} rejected: {(probe or {}).get('error')}", flush=True)
            return
        print(f"[mash] {button} for 3s (by {user}@{platform})", flush=True)
        for _ in range(12):
            await asyncio.sleep(0.18)
            post(f"/press/{button}", user=user, platform=platform)


async def handle_message(text: str, user: str, platform: str):
    text = text.strip()
    if not text.startswith("!"):
        return
    parts = text[1:].lower().split(maxsplit=1)
    if not parts:
        return
    cmd = parts[0]
    arg = parts[1] if len(parts) > 1 else ""

    if cmd in EFFECTS:
        await fire_effect(cmd, user, platform)
    elif cmd == "neutral":
        if State.active_reset_task and not State.active_reset_task.done():
            State.active_reset_task.cancel()
        post("/neutral",       user=user, platform=platform)
        post("/overlay/clear", user=user, platform=platform)
        post("/release",       user=user, platform=platform)
        State.active_label = None
        print(f"[reset] by {user}@{platform}", flush=True)
    elif cmd == "press":
        await fire_press(arg, user, platform, "tap")
    elif cmd == "hold":
        await fire_press(arg, user, platform, "hold")
    elif cmd == "mash":
        await fire_press(arg, user, platform, "mash")
    elif cmd == "release":
        post("/release", user=user, platform=platform)
        print(f"[release] by {user}@{platform}", flush=True)


# ----------------------------------------------------------------------------
# WebSocket loop with token refresh + reconnect
# ----------------------------------------------------------------------------

async def main():
    backoff = 1.0
    while True:
        try:
            token = get_access_token()
        except Exception as e:
            sys.exit(f"[restream-bot] auth error: {e}")

        ws_url = f"{WS_URL_BASE}?accessToken={token}"
        try:
            async with websockets.connect(ws_url, ping_interval=30) as ws:
                print(f"[restream-bot] connected to {WS_URL_BASE}", flush=True)
                print(f"[restream-bot] bridge at {BRIDGE_URL}", flush=True)
                backoff = 1.0
                async for raw in ws:
                    if DEBUG:
                        print(f"[ws raw] {raw}", flush=True)
                    try:
                        ev = json.loads(raw)
                    except Exception:
                        continue
                    text, user, platform = extract_message(ev)
                    if text:
                        await handle_message(text, user or "?", platform or "?")
        except websockets.ConnectionClosed as e:
            print(f"[restream-bot] WS closed: {e.code} {e.reason}", flush=True)
        except Exception as e:
            print(f"[restream-bot] WS error: {e}", flush=True)

        # exponential backoff, cap at 60s
        wait = min(backoff, 60.0)
        print(f"[restream-bot] reconnecting in {wait:.1f}s…", flush=True)
        await asyncio.sleep(wait)
        backoff *= 2.0


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
