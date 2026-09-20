// app.c - USB2WIIEXT App Entry Point
// USB host input → Wii extension-port I2C slave output.
//
// The wired sibling of bt2wiiext. The output stage is byte-for-byte the same
// device (wii_ext_device.c, I2C slave 0x52 on GP4/GP5, Classic Controller Pro
// emulation); only the input stage differs — native USB host instead of the
// CYW43 Bluetooth radio.
//
// There is deliberately NO CDC/web-config output interface here. The board's
// native USB port is the host port, so there is no device-side endpoint to
// expose a serial console on — the same situation as usb2dc, and unlike
// bt2wiiext (which leaves USB free because its input arrives over Bluetooth).
// tusb_config.h reflects this: this app defines neither CONFIG_USB nor
// DISABLE_USB_HOST nor CONFIG_NGC nor CONFIG_BT2WIIEXT, so it takes the
// host-only branch with RHPORT0 = OPT_MODE_HOST.

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/device/wii_ext/wii_ext_device.h"
#include "usb/usbh/usbh.h"

#include "pico/stdlib.h"
#include <stdio.h>

// ============================================================================
// INPUT / OUTPUT REGISTRATION
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &usbh_input_interface,
};

const InputInterface** app_get_input_interfaces(uint8_t* count) {
    *count = sizeof(input_interfaces) / sizeof(input_interfaces[0]);
    return input_interfaces;
}

static const OutputInterface* output_interfaces[] = {
    &wii_output_interface,
};

const OutputInterface** app_get_output_interfaces(uint8_t* count) {
    *count = sizeof(output_interfaces) / sizeof(output_interfaces[0]);
    return output_interfaces;
}

// ============================================================================
// INIT
// ============================================================================

void app_init(void)
{
    printf("[app:usb2wiiext] Initializing USB2WIIEXT v%s\n", JOYPAD_VERSION);

    // Expose the Wii output for pin/mode config (OUTPUT.NATIVE.GET/SET).
    // Reachable over the joypad-os CDC channel only on builds that have one;
    // this app has no device port, so it is set for parity with bt2wiiext and
    // for any future config transport rather than for a channel that exists now.
    native_output = &wii_output_interface;

    // Force Classic Controller Pro emulation. Pro reports the same 6-byte
    // format-0x01 layout as a regular Classic but advertises id[0]=0x01
    // so the Wii recognises it as the Pro variant. Matches bt2wiiext.
    wii_device_emulation_t emu = WII_DEV_EMULATE_CLASSIC_PRO;

    // I2C slave up first so a plugged-in Wiimote gets a valid register file
    // immediately — before the router and the USB host stack come up, the
    // ID / calibration / neutral report bytes are already seeded. The Wiimote
    // probes its extension port on connect and will latch "nothing there" if
    // the slave is not answering yet, so ordering here is load-bearing.
    wii_device_init(emu);

    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_WII_EXTENSION] = WII_OUTPUT_PORTS,
        },
        .merge_all_inputs = true,
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 0,
    };
    router_init(&router_cfg);
    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_WII_EXTENSION, 0);

    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    printf("[app:usb2wiiext] Initialization complete\n");
    printf("[app:usb2wiiext]   Routing: USB host -> Wii extension (0x52)\n");
    printf("[app:usb2wiiext]   Emulating: Classic Controller Pro\n");
}

// ============================================================================
// TASK
// ============================================================================

void app_task(void)
{
    // Nothing to pump. Unlike usb2gc — which forwards GameCube rumble back to
    // the USB controllers every tick — the Wii Classic Controller has no
    // rumble motor. The Wiimote owns the only motor in that stack and drives
    // it itself over Bluetooth from the console; nothing reaches the extension
    // port. wii_output_interface exposes no get_rumble for the same reason.
    // Same as bt2wiiext, which also forwards no feedback.
}
