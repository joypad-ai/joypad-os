#!/usr/bin/env python3
"""
joypad-live Twitch chat bot
===========================
Listens to your Twitch chat and translates viewer commands into REST calls
on the local joypad-live bridge (http://127.0.0.1:8777). Effects auto-reset
after a configurable timer so the streamer is never stuck inverted forever.

Setup (~3 minutes):

    1. Get an OAuth chat token for the bot account that will speak in chat
       (often a second Twitch account, but your main works too):

           https://twitchtokengenerator.com/quick/  (chat:read + chat:edit)

       The token looks like 'abc123def456…'. Prefix it with 'oauth:' when
       passing to twitchio (the bot does this automatically).

    2. Install deps:

           pip install twitchio requests

    3. Run the bridge first in another terminal:

           python3 tools/joypad-live/python/server.py /dev/cu.usbmodemXXXX

    4. Run the bot:

           TWITCH_TOKEN=oauth:abc123… \\
           TWITCH_CHANNEL=yourtwitchhandle \\
           python3 tools/joypad-live/twitch-bot/bot.py

Commands viewers can fire (10–15s each, auto-reset):

    !chaos          d-pad scramble (PROFILE.SELECT 2)
    !aswap          A ↔ B          (PROFILE.SELECT 1)
    !invertx        invert both stick X axes
    !invertlx       invert LX only
    !invertrx       invert RX only
    !inverty        invert both stick Y axes
    !invertall      invert ALL stick axes
    !swap           swap L and R sticks
    !fullchaos      swap + invert all
    !neutral        force reset (everyone can spam this)
    !effects        list available commands

Tunables (env vars):

    BRIDGE_URL              http://127.0.0.1:8777
    DEFAULT_DURATION_SEC    12
    OVERLAY_DURATION_SEC    10
"""

import asyncio
import os
import sys

import requests
from twitchio.ext import commands

TOKEN = os.environ.get("TWITCH_TOKEN")
CHANNEL = os.environ.get("TWITCH_CHANNEL")
BRIDGE = os.environ.get("BRIDGE_URL", "http://127.0.0.1:8777")
DEFAULT_DURATION = int(os.environ.get("DEFAULT_DURATION_SEC", "12"))
OVERLAY_DURATION = int(os.environ.get("OVERLAY_DURATION_SEC", "10"))

if not TOKEN or not CHANNEL:
    sys.stderr.write(
        "missing TWITCH_TOKEN or TWITCH_CHANNEL\n"
        "usage: TWITCH_TOKEN=oauth:… TWITCH_CHANNEL=name python3 bot.py\n")
    sys.exit(1)

# twitchio wants the token without the 'oauth:' prefix on some versions,
# but tolerates it. Strip just in case the user pasted both forms.
TOKEN_BARE = TOKEN.removeprefix("oauth:")


def post(path: str, body: dict | None = None,
         user: str | None = None, platform: str = "twitch") -> dict | None:
    """POST to the bridge with viewer attribution headers so the overlay's
    live feed can show 'alice@twitch → !chaos'."""
    headers = {}
    if user:
        headers["X-User"] = user
        headers["X-Platform"] = platform
    try:
        r = requests.post(f"{BRIDGE}{path}", json=body, headers=headers, timeout=2)
        return r.json()
    except Exception as e:
        print(f"[bridge] POST {path} failed: {e}")
        return None


# Effect catalog: command name → (label, post-path, post-body, reset-path, duration)
# Overlays compose ON TOP of any active profile — they don't replace anything,
# so the streamer's current button map survives a chat-driven axis tweak.
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


class Bot(commands.Bot):
    def __init__(self):
        super().__init__(token=TOKEN_BARE, prefix="!", initial_channels=[CHANNEL])
        self.active_label: str | None = None
        self.active_reset_task: asyncio.Task | None = None
        self.active_reset_path: str | None = None

    async def event_ready(self):
        print(f"[bot] connected as {self.nick}, listening on #{CHANNEL}")
        print(f"[bot] bridge at {BRIDGE}")

    async def _arm_reset(self, reset_path: str, secs: int, label: str):
        try:
            await asyncio.sleep(secs)
        except asyncio.CancelledError:
            return
        # auto-reset has no user attribution
        post(reset_path, user="system")
        if self.active_label == label:
            self.active_label = None
            self.active_reset_path = None
        print(f"[effect] {label} → reset")

    async def _fire_effect(self, ctx, key: str):
        if key not in EFFECTS:
            return
        label, path, body, reset_path, secs = EFFECTS[key]

        # If an effect is already running, queue-skip: cancel the prior reset,
        # apply the new one, set new reset timer. This stacks gracefully.
        if self.active_reset_task and not self.active_reset_task.done():
            self.active_reset_task.cancel()

        r = post(path, body, user=ctx.author.name)
        if r is None or not r.get("ok"):
            await ctx.send(f"🛑 bridge error firing {key}")
            return

        self.active_label = label
        self.active_reset_path = reset_path
        self.active_reset_task = asyncio.create_task(
            self._arm_reset(reset_path, secs, label))
        print(f"[effect] {label} for {secs}s (by {ctx.author.name})")
        await ctx.send(f"🎮 {ctx.author.name} → {label} for {secs}s")

    @commands.command(name="chaos")
    async def chaos(self, ctx):     await self._fire_effect(ctx, "chaos")
    @commands.command(name="aswap")
    async def aswap(self, ctx):     await self._fire_effect(ctx, "aswap")
    @commands.command(name="invertx")
    async def invertx(self, ctx):   await self._fire_effect(ctx, "invertx")
    @commands.command(name="invertlx")
    async def invertlx(self, ctx):  await self._fire_effect(ctx, "invertlx")
    @commands.command(name="invertrx")
    async def invertrx(self, ctx):  await self._fire_effect(ctx, "invertrx")
    @commands.command(name="inverty")
    async def inverty(self, ctx):   await self._fire_effect(ctx, "inverty")
    @commands.command(name="invertall")
    async def invertall(self, ctx): await self._fire_effect(ctx, "invertall")
    @commands.command(name="swap")
    async def swap(self, ctx):      await self._fire_effect(ctx, "swap")
    @commands.command(name="fullchaos")
    async def fullchaos(self, ctx): await self._fire_effect(ctx, "fullchaos")

    @commands.command(name="neutral")
    async def neutral(self, ctx):
        if self.active_reset_task and not self.active_reset_task.done():
            self.active_reset_task.cancel()
        post("/neutral", user=ctx.author.name)
        post("/overlay/clear", user=ctx.author.name)
        self.active_label = None
        self.active_reset_path = None
        await ctx.send("🛑 reset to neutral")

    @commands.command(name="effects")
    async def effects(self, ctx):
        names = " ".join(f"!{k}" for k in EFFECTS)
        await ctx.send(
            f"remap: {names} !neutral  |  "
            f"press: !press <btn> !hold <btn> !mash <btn>  "
            f"(btn: a b x y up down left right start select l1 r1 l2 r2 l3 r3 home)"
        )

    # ------------------------------------------------------------------
    # Direct button injection — chat can press buttons, not just remap.
    # The synthetic events come in on a separate router slot so they
    # merge with the streamer's real controller input.
    # ------------------------------------------------------------------

    @commands.command(name="press")
    async def press(self, ctx, button: str = ""):
        button = button.strip().lower()
        if not button:
            await ctx.send(f"usage: !press <btn>  — e.g. !press a, !press up")
            return
        r = post(f"/press/{button}", user=ctx.author.name)
        if r is None:
            await ctx.send("🛑 bridge unreachable")
        elif not r.get("ok"):
            await ctx.send(f"🛑 {r.get('error', 'unknown error')}")
        else:
            await ctx.send(f"🎮 {ctx.author.name} → tap {button.upper()}")

    @commands.command(name="hold")
    async def hold(self, ctx, button: str = "", *_args):
        button = button.strip().lower()
        if not button:
            await ctx.send("usage: !hold <btn>  — auto-releases after 2s")
            return
        r = post(f"/hold/{button}", user=ctx.author.name)
        if r is None or not r.get("ok"):
            err = (r or {}).get("error", "bridge unreachable")
            await ctx.send(f"🛑 {err}")
            return
        await ctx.send(f"🎮 {ctx.author.name} → hold {button.upper()} (2s)")
        await asyncio.sleep(2.0)
        post("/release", user="system")

    @commands.command(name="mash")
    async def mash(self, ctx, button: str = ""):
        button = button.strip().lower()
        if not button:
            await ctx.send("usage: !mash <btn>  — rapid taps for 3s")
            return
        # Probe the name once so we can fail fast with a useful chat message.
        probe = post(f"/press/{button}", user=ctx.author.name)
        if probe is None or not probe.get("ok"):
            await ctx.send(f"🛑 {(probe or {}).get('error', 'bridge unreachable')}")
            return
        await ctx.send(f"🎮 {ctx.author.name} → MASH {button.upper()} for 3s!")
        # Already fired one tap via probe; do 12 more at ~200ms intervals.
        for _ in range(12):
            await asyncio.sleep(0.18)
            post(f"/press/{button}", user=ctx.author.name)

    @commands.command(name="release")
    async def release(self, ctx):
        # Manual override in case a !hold gets stuck somehow.
        post("/release")
        await ctx.send("🛑 released synthetic buttons")


if __name__ == "__main__":
    Bot().run()
