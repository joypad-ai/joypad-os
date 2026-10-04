# Zephyr link-layer patches (nRF52840)

The Nintendo Switch 2 runs its controller link at a **5 ms connection interval** —
4 units of 1.25 ms, below the 7.5 ms Bluetooth minimum. Nordic's SoftDevice
Controller refuses it outright, which is why the nRF `universal` build selects
Zephyr's open-source link layer instead (`CONFIG_BT_LL_SW_SPLIT` plus the
devicetree `chosen` swap in `boards/makerdiary_nrf52840.overlay`).

Zephyr's link layer refuses it too, in three places. These patches lower the
minimum to 4 units.

## Why not `CONFIG_BT_CTLR_CONN_INTERVAL_LOW_LATENCY`

Zephyr has that option, but it is a *different, non-standard* feature: it
reinterprets interval values below 6 as **500 µs** units, so the console's 4
would be read as 2.5 ms and the timing would be wrong. These patches keep
standard 1.25 ms units and simply accept 4 and 5.

## What is patched

| File | Check |
|---|---|
| `ull_peripheral.c` | CONNECT_IND interval range — the reconnect/wake case, where the console opens at 5 ms immediately |
| `ull_llcp_conn_upd.c` via `ull_internal.h` | `CONN_INTERVAL_MIN` — the first-pairing case, where the console connects at 15 ms then moves the link to 5 ms |
| `ull_conn.c` | the `>= BT_HCI_LE_INTERVAL_MIN` comparisons that pick between 1.25 ms and 500 µs units, so 4 and 5 keep standard units |

## Applying

```sh
nrf/patches/apply.sh        # idempotent; re-run after `make init-nrf` or `west update`
```

## The interval patch is NOT needed to pair

Decoded captures of a real pairing (2026-09-27) show the console opens a
first-time connection at **15 ms** (CONNECT_IND interval 12); only a bonded
reconnect or wake uses 5 ms. So the interval patch buys nothing at the pairing
stage, while making the link layer a hand-modified component that no working
Switch 2 emulator uses — the riskiest thing in the stack, in the path of the
thing that was failing.

It is therefore carried as `0001-ll-accept-5ms-conn-interval.patch.optional`
and **`apply.sh` does not apply it**. Re-enable it (drop the `.optional`) only
once pairing is proven, when the console tries to move the link to 5 ms.

`0002-ll-radio-diagnostics.patch` is always applied. It changes no behaviour: it
counts scan requests and connection requests at the radio, before any acceptance
check, so "the console never tried" can be told apart from "our link layer turned
it down". Read them with `SWITCH2.SCAN` over CDC. Counters rather than log marks,
because marking every scan request floods the small diagnostic ring in a busy
room, and a printf from the radio ISR hangs the chip outright.

Status: **the advertisement is verified correct on air from the board itself**
(public Nintendo-OUI address, payload byte-identical to the capture). A completed
connection has not yet been observed — see `.dev/docs/switch2-ble-output-plan.md`.
