// msc_host.c — TinyUSB MSC host glue
// See header for design.
//
// Phase 1 (this file): capture the dev_addr/lun of the first MSC device
// to enumerate, log capacity. Block-IO functions are stubs that will
// be filled in once the smoke test confirms enumeration works alongside
// HID controllers via a hub.

#include "msc_host.h"
#include "tusb.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

static struct {
    uint8_t  dev_addr;
    uint8_t  lun;
    uint32_t block_count;
    uint32_t block_size;
    bool     mounted;
} g_msc;

void msc_host_init(void)
{
    memset(&g_msc, 0, sizeof(g_msc));
}

bool msc_host_mounted(void)         { return g_msc.mounted; }
uint32_t msc_host_block_count(void) { return g_msc.block_count; }
uint32_t msc_host_block_size(void)  { return g_msc.block_size; }

// ---- TinyUSB MSC host callbacks --------------------------------------------

// Inquiry response handler — fires once after enumeration so we can
// publish capacity. This is the actual "mounted" trigger; the
// tuh_msc_mount_cb below only signals that the device exists.
static bool inquiry_cb(uint8_t dev_addr, tuh_msc_complete_data_t const* cb_data)
{
    msc_cbw_t const* cbw = cb_data->cbw;
    msc_csw_t const* csw = cb_data->csw;
    if (csw->status != 0) {
        printf("[msc] dev=%u inquiry failed (status=%u)\n", dev_addr, csw->status);
        return false;
    }

    g_msc.dev_addr    = dev_addr;
    g_msc.lun         = cbw->lun;
    g_msc.block_count = tuh_msc_get_block_count(dev_addr, cbw->lun);
    g_msc.block_size  = tuh_msc_get_block_size(dev_addr, cbw->lun);
    g_msc.mounted     = true;

    uint64_t total_bytes = (uint64_t)g_msc.block_count * (uint64_t)g_msc.block_size;
    printf("[msc] dev=%u lun=%u mounted: %lu blocks x %lu bytes (%llu MB)\n",
           dev_addr, cbw->lun,
           (unsigned long)g_msc.block_count,
           (unsigned long)g_msc.block_size,
           (unsigned long long)(total_bytes / (1024ULL * 1024ULL)));
    return true;
}

void tuh_msc_mount_cb(uint8_t dev_addr)
{
    printf("[msc] dev=%u attached, kicking off inquiry\n", dev_addr);
    // Hard-coded LUN 0 — flash drives almost always present a single LUN.
    // TinyUSB will issue READ CAPACITY internally to populate
    // tuh_msc_get_block_count/size before our inquiry_cb runs.
    static scsi_inquiry_resp_t inquiry_resp;  // must outlive the async call
    if (!tuh_msc_inquiry(dev_addr, 0, &inquiry_resp, inquiry_cb, 0)) {
        printf("[msc] dev=%u inquiry submit failed\n", dev_addr);
    }
}

void tuh_msc_umount_cb(uint8_t dev_addr)
{
    if (g_msc.mounted && g_msc.dev_addr == dev_addr) {
        printf("[msc] dev=%u detached\n", dev_addr);
        memset(&g_msc, 0, sizeof(g_msc));
    }
}

// ---- Block IO --------------------------------------------------------------
// Synchronous wrapper over the async TinyUSB MSC API: submit the SCSI
// READ10/WRITE10 and pump tuh_task() until the completion callback fires
// (which also keeps the HID controller on the same hub serviced) or we time
// out. Caller is on Core 0 and never inside tuh_task(), so no re-entrancy.

static volatile bool s_io_done;
static volatile bool s_io_ok;

static bool io_complete_cb(uint8_t dev_addr, tuh_msc_complete_data_t const* cb_data)
{
    (void)dev_addr;
    s_io_ok = (cb_data->csw->status == 0);
    s_io_done = true;
    return true;
}

static bool msc_io(bool write, uint32_t lba, void* buf, uint32_t count)
{
    if (!g_msc.mounted || count == 0) return false;
    s_io_done = false;
    s_io_ok = false;
    bool submitted = write
        ? tuh_msc_write10(g_msc.dev_addr, g_msc.lun, buf, lba, (uint16_t)count, io_complete_cb, 0)
        : tuh_msc_read10 (g_msc.dev_addr, g_msc.lun, buf, lba, (uint16_t)count, io_complete_cb, 0);
    if (!submitted) return false;

    uint32_t start = to_ms_since_boot(get_absolute_time());
    while (!s_io_done) {
        tuh_task();
        if (to_ms_since_boot(get_absolute_time()) - start > 3000) return false;  // timeout
    }
    return s_io_ok;
}

bool msc_host_read_blocks(uint32_t lba, void* buf, uint32_t count)
{
    return msc_io(false, lba, buf, count);
}

bool msc_host_write_blocks(uint32_t lba, const void* buf, uint32_t count)
{
    return msc_io(true, lba, (void*)buf, count);
}
