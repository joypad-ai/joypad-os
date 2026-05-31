// vmu_storage.h - Persistence backend selector for the Dreamcast VMU.
//
// Picks one backing store for the 128 KB vmu_ram image at startup, by
// priority: USB flash drive > SD card > onboard QSPI flash > RAM-only.
// Each backend is independently gated at compile time:
//   - USB MSC : CONFIG_VMU_USB   (needs a USB hub sharing the host port)
//   - SD card : CONFIG_SD        (delegates to the proven vmu_sd.c)
//   - QSPI    : CONFIG_VMU_QSPI  (opt-in; reserves a 128 KB flash region)
// With none enabled/available the card is RAM-only and reverts to the
// preformat default (Joypad OS ICONDATA) each boot.
//
// Only a single VMU is supported — two full 128 KB images don't fit in
// RP2040 SRAM, so multi-VMU is intentionally out of scope here.
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    VMU_BACKEND_NONE = 0,   // RAM-only (preformat default)
    VMU_BACKEND_QSPI,       // onboard flash partition
    VMU_BACKEND_SD,         // SD card (DC_1.VMU)
    VMU_BACKEND_USB,        // USB mass-storage flash drive
} vmu_backend_t;

// Probe backends in priority order and bind the first available one,
// loading its stored image into vmu_ram (or seeding it from the
// preformat default already present). Call once from Core 0 after maple
// enumeration (same timing as the old vmu_sd_init()).
void vmu_storage_init(void);

// Periodic debounced flush of dirty vmu_ram to the active backend.
// Call from Core 0 main loop (via vmu_task()).
void vmu_storage_task(void);

// Which backend ended up active (for logging / device-info).
vmu_backend_t vmu_storage_backend(void);
const char*   vmu_storage_backend_name(void);
