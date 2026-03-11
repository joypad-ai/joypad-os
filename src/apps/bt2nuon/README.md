# bt2nuon — Bluetooth to Nuon Adapter (Pico W)

Bluetooth controller input to Nuon DVD player via the Polyface PIO protocol.

## Hardware

- **Board**: Raspberry Pi Pico W
- **Input**: Bluetooth Classic + BLE controllers (via CYW43)
- **Output**: Nuon Polyface protocol on GPIO 2 (data) / GPIO 3 (clock)
- **UART**: GPIO 0 (TX) / GPIO 1 (RX) at 115200 baud

## Key Constraint: No BOOTSEL Button Service

The BOOTSEL button on Pico W reads the QSPI flash chip select pin. This
requires temporarily disabling flash access and all interrupts (~8µs per read).
When called every main loop iteration, this disrupts polyface timing and causes
the Nuon console to lose detection.

**Fix**: `DISABLE_BUTTON_SERVICE=1` — BOOTSEL button is not used. BT scanning
starts automatically at boot. Use UART 'B' command for bootloader.

## Software Mitigations

- Both polyface PIO programs on PIO0 (14 + 18 = 32 instructions), PIO1 reserved for CYW43
- SIO-based turnaround delay (`gpio_get()`) instead of `pio_sm_exec()` to minimize APB bus contention
- Core 1 high bus priority via BUSCTRL
- All Core 1 code in SRAM (`__no_inline_not_in_flash_func`)
- Deferred BT init (10s) to let polyface establish before CYW43 powers up
- Reduced scan parameters (`SCAN_INTERVAL=0x0500`, `SCAN_WINDOW=0x0030`)
- All remaining PIO0 SMs claimed to prevent CYW43 from using PIO0

## Build

```bash
make bt2nuon_pico_w
make flash-bt2nuon_pico_w
```

## Usage

- BT scanning starts automatically after boot
- **UART 'B'**: Reboot to UF2 bootloader
