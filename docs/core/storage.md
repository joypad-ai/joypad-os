# Flash Persistence

The storage subsystem provides a unified API for persisting settings across power cycles. It wraps platform-specific flash backends with write throttling and dual-core safety.

Source: `src/core/services/storage/storage.h`, `storage.c`, `flash.h`, and `flash.c`

## What Gets Stored

The `flash_t` structure (256 bytes, exactly one flash page) holds:

| Field | Size | Description |
|-------|------|-------------|
| `magic` | 4B | Validation magic (0x47435052 = "GCPR") |
| `sequence` | 4B | Monotonic sequence number (higher = newer, 0xFFFFFFFF = empty) |
| `active_profile_index` | 1B | Currently selected profile (0 = default, 1-4 = custom) |
| `usb_output_mode` | 1B | USB device output mode (HID, XInput, PS3, etc.) |
| `wiimote_orient_mode` | 1B | Wiimote orientation (Auto/Horizontal/Vertical) |
| `custom_profile_count` | 1B | Number of custom profiles (0-4) |
| `bt_output_mode` | 1B | BT output mode (BLE / Classic) |
| `reserved` | 19B | Reserved for future settings |
| `profiles[4]` | 224B | Up to 4 custom profiles (56 bytes each) |

Custom profiles (`custom_profile_t`) store per-button remap values, stick sensitivity, inversion flags, and SOCD mode. Created via the web configuration tool.

## Platform Backends

| Platform | Backend | Location | Safety Mechanism |
|----------|---------|----------|------------------|
| RP2040 | Raw flash (last sectors before BTstack area) | `src/core/services/storage/flash.c` | `flash_safe_execute()` for dual-core coordination |
| ESP32-S3 | NVS (Non-Volatile Storage) | `esp/main/flash_esp32.c` | Mutex-protected by ESP-IDF |
| nRF52840 | Zephyr NVS | `nrf/src/flash_nrf.c` | Mutex-protected by Zephyr |

## RP2040 Flash Layout

The RP2040 implementation uses journaled storage in two 4KB flash sectors near the end of flash:

```
Flash layout (from end):
  ... firmware code ...
  [Sector B: 4KB]  -- 16 x 256-byte journal slots (backup)
  [Sector A: 4KB]  -- 16 x 256-byte journal slots (primary)
  [BTstack: 8KB]   -- Bluetooth bond storage
  [RP2040 flash end]
```

### Journal Design

Each 4KB sector holds 16 x 256-byte slots arranged as a ring buffer:

1. Each save writes to the next empty slot via page program (~1ms, no erase needed)
2. The `sequence` field identifies the newest entry (highest number wins)
3. When all 16 slots are full, the sector is erased (~45ms) and writing restarts from slot 0
4. Sector erase is deferred if Bluetooth is active to avoid latency spikes

This design minimizes flash wear and avoids the long erase latency on the hot path.

## Write Throttling

Changes are debounced with a 5-second delay:

1. `flash_save()` sets a pending flag and records the new settings
2. `flash_task()` (called every main loop iteration) checks the timer
3. After 5 seconds with no new save requests, the actual flash write occurs
4. `flash_save_now()` bypasses debouncing for immediate writes
5. `flash_save_force()` also bypasses the BT-active check (used before reboot)

This prevents rapid profile cycling from wearing out flash (RP2040 flash supports roughly 100K erase cycles per sector).

## Dual-Core Safety (RP2040)

Flash operations on RP2040 require special care because flash is memory-mapped and shared between both cores:

- `flash_safe_execute()` (from pico-sdk) coordinates between cores -- it pauses Core 1, disables interrupts, performs the flash operation, then resumes
- If `flash_safe_execute()` fails (e.g., Core 1 cannot be paused), the code falls back to a direct flash operation with interrupts disabled
- Core 1 code that runs during flash writes must be in RAM (`__not_in_flash_func`) to avoid crashes

## API

```c
// Init and main loop
storage_init();       // Call once at boot (loads settings from flash)
storage_task();       // Call every loop iteration (handles debounced writes)

// Read/write settings
flash_t* settings = flash_get_settings();  // Get runtime settings pointer
flash_save(settings);                       // Debounced save (5s delay)
flash_save_now(settings);                   // Immediate save
flash_save_force(settings);                 // Immediate, ignores BT state

// Profile helpers
flash_get_active_profile_index();    // 0=default, 1-4=custom
flash_set_active_profile_index(2);   // Switch to custom profile 2
flash_cycle_profile_next();          // Next profile (wraps)
flash_cycle_profile_prev();          // Previous profile (wraps)
```

## BT-Aware Writes

On RP2040 with Bluetooth active, sector erase can cause BT latency spikes. The flash system tracks whether BT is connected:

- `flash_on_bt_disconnect()` -- Notifies flash that BT is idle, triggering any pending writes
- `flash_has_pending_write()` -- Check if a write is waiting for BT to become idle

## See Also

- [Profiles](profiles.md) -- What gets persisted (profile selections)
- [Platform HAL](platform-hal.md) -- Platform abstraction layer
