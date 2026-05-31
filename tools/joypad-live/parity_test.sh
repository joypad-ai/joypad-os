#!/usr/bin/env bash
# parity_test.sh — run the same endpoint sequence against both bridges and diff.
# Proves the C# and Python REST bridges are drop-in compatible (same wire
# protocol, same HTTP contract, same JSON responses byte-for-byte).
#
# Usage:
#   bash tools/joypad-live/parity_test.sh <serial-port>
#   e.g. bash tools/joypad-live/parity_test.sh /dev/cu.usbmodemDF6254209F574
#
# Requires: jq (for canonical JSON comparison), python3 + pyserial, dotnet.

set -e

PORT="${1:?usage: $0 <serial-port>}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
TMPDIR=$(mktemp -d)
cleanup() {
    rm -rf "$TMPDIR"
    pkill -f 'joypad-live/python/server.py' 2>/dev/null || true
    pkill -f 'JoypadLive' 2>/dev/null || true
}
trap cleanup EXIT

# Endpoint sequence to exercise. Deterministic so responses are comparable.
# Order matters: each /profiles call captures the "active" set by the preceding
# PROFILE.SET, so we verify state transitions match too. /apply is tested with
# a real button_map; /profiles after /apply confirms the ephemeral override is
# invisible to PROFILE.LIST (active should still reflect the flash-stored one).
exercise() {
    local label="$1" outdir="$2"
    mkdir -p "$outdir"
    STEP_I=0

    step() {
        local method="$1" path="$2" body="$3"
        STEP_I=$((STEP_I + 1))
        local file="$outdir/$(printf %02d $STEP_I)_${method}${path//\//_}.json"
        if [ -n "$body" ]; then
            curl -s -X "$method" -H "Content-Type: application/json" -d "$body" \
                "http://127.0.0.1:8777$path" | jq -S . > "$file"
        elif [ "$method" = "POST" ]; then
            curl -s -X POST -d '' "http://127.0.0.1:8777$path" | jq -S . > "$file"
        else
            curl -s "http://127.0.0.1:8777$path" | jq -S . > "$file"
        fi
        echo "  $(printf %2d $STEP_I). $method $path  ->  $(jq -c . "$file")"
    }

    step POST /neutral
    step GET  /health
    step GET  /info
    step GET  /profiles
    step POST /profile/1
    step GET  /profiles
    step POST /profile/2
    step GET  /profiles
    # PROFILE.APPLY with a non-trivial button_map (face + d-pad scramble).
    # After /apply, /profiles.active should still reflect the flash-stored
    # selection (2/Chaos here) — ephemeral is invisible to PROFILE.LIST.
    step POST /apply '{"name":"ParityTest","button_map":[2,1,4,3,0,0,0,0,0,0,0,0,15,16,14,13,0,0]}'
    step GET  /profiles
    step POST /clear
    # OVERLAY.SET — runtime tweak layered on top of whatever's active.
    # flags=8 = INVERT_LX, plus a stick-sens tweak to exercise multiple fields.
    step POST /overlay '{"flags":8,"left_stick_sens":120,"socd_mode":2}'
    step POST /overlay/clear
    step POST /neutral
    step GET  /profiles
}

wait_for_health() {
    local tries=20
    while ! curl -fs http://127.0.0.1:8777/health >/dev/null 2>&1; do
        tries=$((tries - 1))
        [ $tries -le 0 ] && { echo "bridge never came up"; return 1; }
        sleep 0.3
    done
}

echo "============================================================"
echo "Phase 1: C# bridge"
echo "============================================================"
pkill -f "joypad-live/python/server.py" 2>/dev/null || true
pkill -f "JoypadLive" 2>/dev/null || true
sleep 0.5
export DOTNET_ROOT="/opt/homebrew/opt/dotnet/libexec"
cd "$REPO/tools/joypad-live/csharp"
dotnet build -nologo --no-restore > /dev/null 2>&1 || dotnet build -nologo > /dev/null 2>&1
nohup dotnet run --no-build -- "$PORT" > "$TMPDIR/csharp.log" 2>&1 &
CSHARP_PID=$!
cd "$REPO"
wait_for_health
exercise csharp "$TMPDIR/csharp"
kill $CSHARP_PID 2>/dev/null || true
sleep 0.8

echo ""
echo "============================================================"
echo "Phase 2: Python bridge"
echo "============================================================"
nohup python3 "$REPO/tools/joypad-live/python/server.py" "$PORT" > "$TMPDIR/python.log" 2>&1 &
PY_PID=$!
wait_for_health
exercise python "$TMPDIR/python"
kill $PY_PID 2>/dev/null || true

echo ""
echo "============================================================"
echo "Diff (canonical JSON, jq -S)"
echo "============================================================"
mismatches=0
for f in "$TMPDIR/csharp"/*.json; do
    name=$(basename "$f")
    if ! diff -q "$TMPDIR/csharp/$name" "$TMPDIR/python/$name" > /dev/null; then
        echo "DIFFER: $name"
        diff "$TMPDIR/csharp/$name" "$TMPDIR/python/$name" || true
        mismatches=$((mismatches + 1))
    else
        echo "  ok: $name"
    fi
done

echo ""
if [ $mismatches -eq 0 ]; then
    echo "✓ PARITY VERIFIED — all responses byte-identical between C# and Python bridges."
else
    echo "✗ $mismatches mismatches"
    exit 1
fi
