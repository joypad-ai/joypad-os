// vmu_storage.c - Persistence backend selector for the Dreamcast VMU.
//
// Probes USB flash > SD card > QSPI > RAM at startup and binds one backend
// for the 128 KB vmu_ram image. SD is delegated to the proven vmu_sd.c; the
// QSPI backend lives here. See vmu_storage.h for the gating flags.

#include "vmu_storage.h"
#include "vmu.h"
#include "vmu_sd.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

#ifdef CONFIG_VMU_QSPI
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#endif

#ifdef CONFIG_VMU_USB
#include "tusb.h"
#include "ff.h"
#include "msc_host.h"
#endif

// The 128 KB VMU image and the Core-1-set dirty flag live in vmu.c.
extern uint8_t vmu_ram[];
extern volatile bool vmu_dirty_flag;
extern volatile uint8_t vmu_dirty_blocks[];   // per-block dirty bitmap (USB)

static vmu_backend_t active = VMU_BACKEND_NONE;

// ===========================================================================
// QSPI backend
// ===========================================================================
#ifdef CONFIG_VMU_QSPI

// Reserve the 128 KB ending 256 KB below the top of flash. The top ~16 KB is
// used by the settings journal + BTstack bank (see flash.c); 256 KB down
// leaves a comfortable gap and sits far above the firmware. Sector-aligned
// because 256 KB and 128 KB are both multiples of the 4 KB erase sector.
#define VMU_QSPI_OFFSET     (PICO_FLASH_SIZE_BYTES - (256u * 1024u))
#define VMU_QSPI_WRITEBACK_MS  2000   // debounce; longer than SD — flash erase is slow

_Static_assert(VMU_IMAGE_SIZE % FLASH_SECTOR_SIZE == 0,
               "VMU image must be a whole number of flash sectors");

static volatile uint32_t qspi_last_write_ms = 0;

static const uint8_t* qspi_xip(void) {
    return (const uint8_t*)(XIP_BASE + VMU_QSPI_OFFSET);
}

// Worker run under flash_safe_execute (or the IRQ-disabled fallback): erase
// one 4 KB sector and reprogram it. flash_range_* are RAM-resident in the SDK.
typedef struct { uint32_t offset; const uint8_t* data; } qspi_sector_t;
static void qspi_sector_worker(void* arg) {
    qspi_sector_t* s = (qspi_sector_t*)arg;
    flash_range_erase(s->offset, FLASH_SECTOR_SIZE);
    flash_range_program(s->offset, s->data, FLASH_SECTOR_SIZE);
}

// Erase+program only the sectors that differ from what's already in flash —
// a typical save touches 1-3 of the 32 sectors, so this minimizes both wear
// and the time spent with XIP paused (and thus maple stalled).
static void qspi_flush(void) {
    const uint8_t* xip = qspi_xip();
    for (uint32_t off = 0; off < VMU_IMAGE_SIZE; off += FLASH_SECTOR_SIZE) {
        if (memcmp(vmu_ram + off, xip + off, FLASH_SECTOR_SIZE) == 0) continue;
        qspi_sector_t s = { VMU_QSPI_OFFSET + off, vmu_ram + off };
        int r = flash_safe_execute(qspi_sector_worker, &s, UINT32_MAX);
        if (r != PICO_OK) {
            // Fallback if the other core isn't a registered lockout victim.
            uint32_t ints = save_and_disable_interrupts();
            flash_range_erase(VMU_QSPI_OFFSET + off, FLASH_SECTOR_SIZE);
            flash_range_program(VMU_QSPI_OFFSET + off, vmu_ram + off, FLASH_SECTOR_SIZE);
            restore_interrupts(ints);
        }
    }
}

static bool qspi_init(void) {
    // vmu_ram already holds the preformat default (root magic = 0x55*16 at
    // block 255). If the flash copy is also formatted, load it; otherwise
    // seed flash from the default we were handed.
    const uint8_t* root = qspi_xip() + (uint32_t)(VMU_TOTAL_BLOCKS - 1) * VMU_BLOCK_SIZE;
    bool formatted = true;
    for (int i = 0; i < 16; i++) {
        if (root[i] != 0x55) { formatted = false; break; }
    }
    if (formatted) {
        memcpy(vmu_ram, qspi_xip(), VMU_IMAGE_SIZE);
        printf("[vmu-qspi] Loaded VMU image from flash\n");
    } else {
        printf("[vmu-qspi] No saved image — seeding flash from preformat default\n");
        qspi_flush();
    }
    return true;
}

static void qspi_task(void) {
    if (!vmu_dirty_flag) return;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (qspi_last_write_ms == 0) qspi_last_write_ms = now;
    if (now - qspi_last_write_ms < VMU_QSPI_WRITEBACK_MS) return;
    vmu_dirty_flag = false;
    qspi_last_write_ms = 0;
    qspi_flush();
    printf("[vmu-qspi] Flushed VMU image to flash\n");
}

#endif  // CONFIG_VMU_QSPI

// ===========================================================================
// USB mass-storage backend (flash drive on a hub sharing the host port)
// ===========================================================================
#ifdef CONFIG_VMU_USB

#define VMU_USB_FILENAME      "DC_1.VMU"   // interchangeable with the SD file
#define VMU_USB_WRITEBACK_MS  1500
#define VMU_USB_MOUNT_WAIT_MS 4000         // drive shares the hub; let it enumerate

static FATFS usb_fs;
static volatile uint32_t usb_last_write_ms = 0;

static bool usb_init(void) {
    uint32_t start = to_ms_since_boot(get_absolute_time());
    while (!msc_host_mounted()) {
        tuh_task();   // pump enumeration (also services the controller)
        if (to_ms_since_boot(get_absolute_time()) - start > VMU_USB_MOUNT_WAIT_MS) {
            return false;   // no drive — fall through to the next backend
        }
    }
    if (f_mount(&usb_fs, "", 1) != FR_OK) {
        printf("[vmu-usb] f_mount failed\n");
        return false;
    }
    FIL f; UINT n = 0;
    if (f_open(&f, VMU_USB_FILENAME, FA_READ) == FR_OK) {
        f_read(&f, vmu_ram, VMU_IMAGE_SIZE, &n);
        f_close(&f);
        if (n == VMU_IMAGE_SIZE) {
            printf("[vmu-usb] Loaded %s\n", VMU_USB_FILENAME);
            return true;
        }
        printf("[vmu-usb] %s wrong size (%u) — recreating\n", VMU_USB_FILENAME, n);
    }
    // Seed from the preformat default already sitting in vmu_ram.
    if (f_open(&f, VMU_USB_FILENAME, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
        printf("[vmu-usb] failed to create %s\n", VMU_USB_FILENAME);
        return false;
    }
    f_write(&f, vmu_ram, VMU_IMAGE_SIZE, &n);
    f_close(&f);
    printf("[vmu-usb] Created %s\n", VMU_USB_FILENAME);
    return true;
}

// Write only the blocks the DC actually changed (per-block dirty bitmap),
// seeking to each within the existing file — a few KB instead of 128 KB, so
// the maple stall during the blocking USB write stays in the low-ms range.
static void usb_flush_dirty(void) {
    FIL f;
    if (f_open(&f, VMU_USB_FILENAME, FA_WRITE | FA_OPEN_ALWAYS) != FR_OK) return;
    for (uint16_t blk = 0; blk < VMU_TOTAL_BLOCKS; blk++) {
        if (!(vmu_dirty_blocks[blk >> 3] & (1u << (blk & 7)))) continue;
        UINT bw = 0;
        if (f_lseek(&f, (FSIZE_t)blk * VMU_BLOCK_SIZE) == FR_OK &&
            f_write(&f, vmu_ram + (uint32_t)blk * VMU_BLOCK_SIZE,
                    VMU_BLOCK_SIZE, &bw) == FR_OK && bw == VMU_BLOCK_SIZE) {
            vmu_dirty_blocks[blk >> 3] &= (uint8_t)~(1u << (blk & 7));
        }
    }
    f_sync(&f);
    f_close(&f);
}

static void usb_task(void) {
    if (!vmu_dirty_flag) return;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (usb_last_write_ms == 0) usb_last_write_ms = now;
    if (now - usb_last_write_ms < VMU_USB_WRITEBACK_MS) return;
    vmu_dirty_flag = false;
    usb_last_write_ms = 0;
    usb_flush_dirty();
    printf("[vmu-usb] Flushed changed blocks to %s\n", VMU_USB_FILENAME);
}

#endif  // CONFIG_VMU_USB

// ===========================================================================
// Dispatcher
// ===========================================================================

void vmu_storage_init(void) {
    // Priority: USB flash > SD card > QSPI > RAM-only.
#ifdef CONFIG_VMU_USB
    if (usb_init()) { active = VMU_BACKEND_USB; }
    else
#endif
#ifdef CONFIG_SD
    if (vmu_sd_init()) { active = VMU_BACKEND_SD; }
    else
#endif
#ifdef CONFIG_VMU_QSPI
    if (qspi_init()) { active = VMU_BACKEND_QSPI; }
    else
#endif
    { active = VMU_BACKEND_NONE; }

    printf("[vmu-storage] Backend: %s\n", vmu_storage_backend_name());
}

void vmu_storage_task(void) {
    switch (active) {
#ifdef CONFIG_VMU_USB
        case VMU_BACKEND_USB:  usb_task(); break;
#endif
#ifdef CONFIG_SD
        case VMU_BACKEND_SD:   vmu_sd_task(); break;
#endif
#ifdef CONFIG_VMU_QSPI
        case VMU_BACKEND_QSPI: qspi_task(); break;
#endif
        default: break;
    }
}

vmu_backend_t vmu_storage_backend(void) { return active; }

const char* vmu_storage_backend_name(void) {
    switch (active) {
        case VMU_BACKEND_USB:  return "USB flash";
        case VMU_BACKEND_SD:   return "SD card";
        case VMU_BACKEND_QSPI: return "QSPI flash";
        default:               return "RAM-only";
    }
}
