#!/usr/bin/env python3
"""
Simulate the Streamer.bot crowd-control loop against the local REST bridge:
random redeems arrive every few seconds, each effect lasts N seconds, then we
return to neutral. Proves the queue/timer behaviour before wiring real Twitch.

    python3 simulate_crowd_control.py            # 5 redeems, 4s effects
    python3 simulate_crowd_control.py --redeems 10 --effect-duration 3 --gap 2
"""

import argparse
import json
import random
import time
import urllib.request


def http(method: str, path: str) -> dict:
    req = urllib.request.Request(f"http://127.0.0.1:8777{path}", method=method)
    with urllib.request.urlopen(req, timeout=2) as r:
        return json.loads(r.read())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--redeems", type=int, default=5)
    ap.add_argument("--effect-duration", type=float, default=4.0)
    ap.add_argument("--gap", type=float, default=2.0,
                    help="seconds between effect end and next redeem")
    args = ap.parse_args()

    profiles = http("GET", "/profiles")
    effects = [p for p in profiles["profiles"] if not p["builtin"]]
    if not effects:
        print("No custom profiles on device — run server.py --define-demo first")
        return
    print(f"Effects available: {[(p['index'], p['name']) for p in effects]}")

    for i in range(1, args.redeems + 1):
        choice = random.choice(effects)
        idx, name = choice["index"], choice["name"]
        print(f"\n[redeem {i}/{args.redeems}] apply {name} (index {idx}) for {args.effect_duration}s")
        r = http("POST", f"/profile/{idx}")
        print(f"  -> {r}")
        time.sleep(args.effect_duration)
        r = http("POST", "/neutral")
        print(f"  reset -> {r}")
        if i < args.redeems:
            print(f"  waiting {args.gap}s for next redeem...")
            time.sleep(args.gap)

    print("\nDone.")


if __name__ == "__main__":
    main()
