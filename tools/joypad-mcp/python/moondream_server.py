#!/usr/bin/env python3
"""Tiny HTTP wrapper around the moondream Python SDK so the TS MCP can speak
the same shape as Ollama's /api/generate.

Usage:
    export MOONDREAM_API_KEY=...    # free tier from https://moondream.ai
    cd tools/joypad-mcp
    source .venv/bin/activate
    python python/moondream_server.py

The MCP points at this via env: MOONDREAM_URL=http://localhost:11500/api/generate

Mac M-series with Photon: ~1s/inference, no GPU memory tied up like Ollama.
"""
import base64
import io
import logging
import os
import sys
import time

from flask import Flask, jsonify, request
from PIL import Image

import moondream as md

API_KEY = os.environ.get("MOONDREAM_API_KEY")
if not API_KEY:
    print("error: set MOONDREAM_API_KEY (free key at https://moondream.ai)", file=sys.stderr)
    sys.exit(1)

PORT = int(os.environ.get("PORT", "11500"))

logging.basicConfig(level=os.environ.get("LOGLEVEL", "INFO"))
log = logging.getLogger("moondream-server")
log.info("loading Moondream3 (Photon, local=True)...")
t0 = time.time()
# Default device in PhotonVL is "cuda" — override to "mps" on Apple Silicon.
import platform
device = "mps" if platform.machine() == "arm64" else "cuda"
log.info("device=%s", device)
model = md.vl(api_key=API_KEY, local=True, device=device)
log.info("loaded in %.1fs", time.time() - t0)

app = Flask(__name__)


@app.get("/health")
def health():
    return jsonify(ok=True, model="moondream3-photon")


@app.post("/api/generate")
def generate():
    body = request.get_json(force=True)
    prompt = body.get("prompt") or ""
    images = body.get("images") or []
    if not images:
        return jsonify(error="missing images"), 400
    img_b64 = images[0]
    img = Image.open(io.BytesIO(base64.b64decode(img_b64))).convert("RGB")
    # Cap the input at 512px on the longest edge — Moondream's vision tower
    # is happiest around 378–512px and our webcam frames are 1920×1440,
    # which adds gratuitous encode latency without quality gain.
    MAX_DIM = 512
    if max(img.size) > MAX_DIM:
        ratio = MAX_DIM / max(img.size)
        img = img.resize((int(img.width * ratio), int(img.height * ratio)), Image.LANCZOS)

    t = time.time()
    # `query` is the natural-language Q&A method — best fit for "what's on the
    # screen" style asks. `caption` and `detect`/`point` exist for other modes.
    out = model.query(img, prompt)
    answer = out.get("answer", "") if isinstance(out, dict) else str(out)
    return jsonify(
        response=answer.strip(),
        model="moondream3-photon",
        eval_count=None,
        ms=int((time.time() - t) * 1000),
    )


if __name__ == "__main__":
    log.info("listening on http://127.0.0.1:%d", PORT)
    app.run(host="127.0.0.1", port=PORT, threaded=True, debug=False)
