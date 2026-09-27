#!/usr/bin/env bash
# Apply the Zephyr link-layer patches this repo needs.
#
# nrf/zephyr is a west workspace, not part of this repo, so `make init-nrf`
# (or any `west update`) restores pristine sources and drops these changes.
# Re-run this afterwards.
#
# Idempotent: already-applied patches are skipped.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
zephyr="${ZEPHYR_BASE:-$here/../zephyr}"

if [ ! -d "$zephyr/subsys/bluetooth/controller" ]; then
    echo "error: no Zephyr tree at $zephyr (run 'make init-nrf' first)" >&2
    exit 1
fi

for patch in "$here"/*.patch; do
    name="$(basename "$patch")"
    if git -C "$zephyr" apply --reverse --check "$patch" >/dev/null 2>&1; then
        echo "already applied: $name"
    elif git -C "$zephyr" apply "$patch"; then
        echo "applied: $name"
    else
        echo "error: $name did not apply — Zephyr may have moved on; re-cut it" >&2
        exit 1
    fi
done
