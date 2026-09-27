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

Status: **builds, never run against a real console.** Whether an nRF52840 can
actually hold a 5 ms link is the open question — see
`.dev/docs/switch2-ble-output-plan.md`.
