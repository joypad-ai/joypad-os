#!/usr/bin/env python3
"""
Restream OAuth2 helper for joypad-live.

One-time browser-based authorization (authorization code flow with PKCE).
Stores access + refresh tokens at ~/.joypad-live/restream-tokens.json with
chmod 600. After that, bot.py reads tokens silently and refreshes as needed.

Usage:
    1. Sign in at https://developers.restream.io and create an app.
       Redirect URI: http://localhost:8888/callback
       Scopes: chat.read (and chat.write if you want bot replies)
    2. Export creds:
           export RESTREAM_CLIENT_ID=...
           export RESTREAM_CLIENT_SECRET=...
    3. Run this:
           python3 oauth.py
       Browser opens → you click "authorize" → tokens land in
       ~/.joypad-live/restream-tokens.json. Done.

The bot module re-uses get_access_token() to read/refresh on demand.
"""

import base64
import hashlib
import http.server
import json
import os
import secrets
import socket
import sys
import threading
import time
import urllib.parse
import webbrowser

import requests

CLIENT_ID     = os.environ.get("RESTREAM_CLIENT_ID", "")
CLIENT_SECRET = os.environ.get("RESTREAM_CLIENT_SECRET", "")
REDIRECT_PORT = int(os.environ.get("RESTREAM_OAUTH_PORT", "8888"))
# REDIRECT_URI must EXACTLY match what's registered in your Restream app
# (developers.restream.io). Override with RESTREAM_REDIRECT_URI if you
# registered a different value than the default below.
REDIRECT_URI  = os.environ.get(
    "RESTREAM_REDIRECT_URI",
    f"http://localhost:{REDIRECT_PORT}/callback")

# Override via env if Restream changes endpoints again.
AUTH_URL   = os.environ.get("RESTREAM_AUTH_URL",  "https://api.restream.io/login")
TOKEN_URL  = os.environ.get("RESTREAM_TOKEN_URL", "https://api.restream.io/oauth/token")

# Space-delimited list. Restream's scope names have moved between versions —
# override via env if needed. chat.read is what the firehose bot needs.
SCOPES = os.environ.get(
    "RESTREAM_SCOPES",
    "chat.read chat.default.read profile.default.read")

TOKEN_DIR  = os.path.expanduser("~/.joypad-live")
TOKEN_FILE = os.path.join(TOKEN_DIR, "restream-tokens.json")


# ----------------------------------------------------------------------------
# Token storage
# ----------------------------------------------------------------------------

def _load_tokens() -> dict | None:
    if not os.path.exists(TOKEN_FILE):
        return None
    try:
        with open(TOKEN_FILE) as f:
            return json.load(f)
    except Exception:
        return None


def _save_tokens(tokens: dict) -> None:
    os.makedirs(TOKEN_DIR, exist_ok=True)
    os.chmod(TOKEN_DIR, 0o700)
    fd = os.open(TOKEN_FILE, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        json.dump(tokens, f, indent=2)


def _is_expired(tokens: dict, slack_sec: int = 60) -> bool:
    expires_at = tokens.get("expires_at", 0)
    return time.time() + slack_sec >= expires_at


def _stamp(tokens: dict) -> dict:
    """Add expires_at absolute timestamp from expires_in delta."""
    if "expires_in" in tokens and "expires_at" not in tokens:
        tokens["expires_at"] = int(time.time()) + int(tokens["expires_in"])
    return tokens


# ----------------------------------------------------------------------------
# PKCE — authorization code flow with proof key (no client secret leak risk
# for the bot itself, even though Restream is a confidential client).
# ----------------------------------------------------------------------------

def _pkce_pair() -> tuple[str, str]:
    verifier = base64.urlsafe_b64encode(secrets.token_bytes(32)).rstrip(b"=").decode()
    challenge = base64.urlsafe_b64encode(
        hashlib.sha256(verifier.encode()).digest()
    ).rstrip(b"=").decode()
    return verifier, challenge


# ----------------------------------------------------------------------------
# Local callback HTTP server — receives the OAuth redirect
# ----------------------------------------------------------------------------

class _CallbackCatcher:
    """One-shot HTTP server that captures ?code=... from the OAuth redirect."""

    def __init__(self):
        self.code: str | None = None
        self.state_in: str | None = None
        self.event = threading.Event()

    def run(self, expected_state: str) -> str:
        catcher = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass  # silence

            def do_GET(self):
                url = urllib.parse.urlparse(self.path)
                if url.path != "/callback":
                    self.send_response(404); self.end_headers(); return
                qs = urllib.parse.parse_qs(url.query)
                catcher.code = (qs.get("code") or [None])[0]
                catcher.state_in = (qs.get("state") or [None])[0]
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.end_headers()
                self.wfile.write("""
                    <!doctype html><meta charset=utf-8>
                    <title>joypad-live • Restream auth</title>
                    <style>body{font:14px system-ui;background:#0d1117;color:#c9d1d9;padding:2rem;text-align:center}
                    h1{color:#3fb950}</style>
                    <h1>auth complete</h1>
                    <p>tokens saved. you can close this tab.</p>
                """.encode("utf-8"))
                catcher.event.set()

        srv = http.server.HTTPServer(("127.0.0.1", REDIRECT_PORT), Handler)
        t = threading.Thread(target=srv.serve_forever, daemon=True)
        t.start()
        # wait up to 5 minutes for the user to authorize
        if not self.event.wait(timeout=300):
            srv.shutdown()
            raise TimeoutError("no OAuth callback within 5 minutes")
        srv.shutdown()
        if self.state_in != expected_state:
            raise RuntimeError("OAuth state mismatch (possible CSRF)")
        if not self.code:
            raise RuntimeError("no authorization code received")
        return self.code


# ----------------------------------------------------------------------------
# Authorize / refresh
# ----------------------------------------------------------------------------

def authorize() -> dict:
    """Run the full interactive OAuth flow. Opens a browser. Blocks."""
    if not CLIENT_ID:
        sys.exit("missing RESTREAM_CLIENT_ID — create an app at developers.restream.io")

    verifier, challenge = _pkce_pair()
    state = secrets.token_urlsafe(16)

    auth_params = {
        "response_type":         "code",
        "client_id":             CLIENT_ID,
        "redirect_uri":          REDIRECT_URI,
        "state":                 state,
        "scope":                 SCOPES,
        # PKCE — Restream's OAuth server may or may not enforce; safe to send.
        "code_challenge":        challenge,
        "code_challenge_method": "S256",
    }
    full_auth_url = AUTH_URL + "?" + urllib.parse.urlencode(auth_params)

    print(f"[oauth] opening browser to authorize joypad-live against Restream…")
    print(f"[oauth] if it doesn't open, paste this URL manually:\n  {full_auth_url}\n")
    webbrowser.open(full_auth_url)

    code = _CallbackCatcher().run(expected_state=state)
    print(f"[oauth] got code, exchanging for tokens…")

    data = {
        "grant_type":    "authorization_code",
        "code":          code,
        "redirect_uri":  REDIRECT_URI,
        "client_id":     CLIENT_ID,
        "code_verifier": verifier,
    }
    if CLIENT_SECRET:
        data["client_secret"] = CLIENT_SECRET

    r = requests.post(TOKEN_URL, data=data, timeout=15)
    if not r.ok:
        sys.exit(f"[oauth] token exchange failed: {r.status_code} {r.text}")
    tokens = _stamp(r.json())
    _save_tokens(tokens)
    print(f"[oauth] tokens written to {TOKEN_FILE}")
    return tokens


def refresh(tokens: dict) -> dict:
    refresh_token = tokens.get("refresh_token")
    if not refresh_token:
        raise RuntimeError("no refresh_token — re-run python3 oauth.py")
    data = {
        "grant_type":    "refresh_token",
        "refresh_token": refresh_token,
        "client_id":     CLIENT_ID,
    }
    if CLIENT_SECRET:
        data["client_secret"] = CLIENT_SECRET
    r = requests.post(TOKEN_URL, data=data, timeout=15)
    if not r.ok:
        raise RuntimeError(f"refresh failed: {r.status_code} {r.text}")
    new_tokens = _stamp(r.json())
    # preserve refresh_token if the server didn't return a fresh one
    if "refresh_token" not in new_tokens:
        new_tokens["refresh_token"] = refresh_token
    _save_tokens(new_tokens)
    return new_tokens


def get_access_token() -> str:
    """Public API for bot.py: returns a live access token, refreshing if needed."""
    tokens = _load_tokens()
    if not tokens:
        raise RuntimeError(
            "no Restream tokens — run `python3 oauth.py` once to authorize")
    if _is_expired(tokens):
        tokens = refresh(tokens)
    return tokens["access_token"]


if __name__ == "__main__":
    authorize()
