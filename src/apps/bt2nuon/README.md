# bt2nuon — Bluetooth to Nuon Adapter (Pico W)

Bluetooth controller input to Nuon DVD player via the Polyface PIO protocol.

## Hardware

- **Board**: Raspberry Pi Pico W
- **Input**: Bluetooth Classic + BLE controllers (via CYW43)
- **Output**: Nuon Polyface protocol on GPIO 2 (data) / GPIO 3 (clock)
- **UART**: GPIO 0 (TX) / GPIO 1 (RX) at 115200 baud

## Known Issue: CYW43 SPI EMI

The Pico W's CYW43 wireless chip communicates with the RP2040 via PIO-based SPI at 31MHz on GPIO 24/25/29. This creates electromagnetic interference that couples to the polyface GPIO inputs (GPIO 2/3), causing the Nuon console to lose detection of the adapter.

**Root cause**: Physical EMI from SPI signal transitions, not bus contention. Proven by isolation testing:
- DMA writes to PIO1 without SPI program running: no interference
- CPU register writes to PIO1: no interference
- Actual PIO1 SPI with GPIO toggling: polyface disrupted

**Polyface works perfectly without CYW43 active.** The issue only occurs when `cyw43_arch_poll()` processes events (which triggers SPI transactions).

### Proposed Hardware Fix (Unverified)

Adding **220-330pF ceramic capacitors** from GPIO 2 and GPIO 3 to GND (close to the Pico W) should low-pass filter the 31MHz SPI noise while passing the 1.9MHz polyface signal. This has **not been tested yet**.

- 330pF: cutoff ~10MHz
- 220pF: cutoff ~15MHz
- 100pF: cutoff ~32MHz (may be marginal)

Do **not** use 100nF or larger — that will kill the 1.9MHz polyface signal.

### Software Mitigations Applied

- Both polyface PIO programs on PIO0 (14 + 18 = 32 instructions), PIO1 reserved for CYW43
- SIO-based turnaround delay (`gpio_get()`) instead of `pio_sm_exec()` to eliminate APB bus contention
- Core 1 high bus priority via BUSCTRL
- All Core 1 code in SRAM (`__no_inline_not_in_flash_func`)
- Deferred BT init (10s) to let polyface establish before CYW43 powers up
- `BTSTACK_DEFER_SCAN` — scanning only starts on BOOTSEL button press
- Reduced scan parameters (`SCAN_INTERVAL=0x0500`, `SCAN_WINDOW=0x0030`)
- All remaining PIO0 SMs claimed to prevent CYW43 from using PIO0

These mitigations are necessary but not sufficient without the hardware filter caps.

## Build

```bash
make bt2nuon_pico_w
make flash-bt2nuon_pico_w
```

## Usage

- **BOOTSEL click**: Start 60s Bluetooth scan
- **BOOTSEL hold**: Disconnect all devices + clear bonds
- **UART 'B'**: Reboot to UF2 bootloader
