// sony_psc.c
//
// Firmware glue for Sony PlayStation Classic. The pure parser lives in
// libjoypad (joypad/devices/sony/psc.h). This file is a thin wrapper that
// delegates the byte → input_event_t translation and submits to the router.

#include "sony_psc.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include <joypad/devices/sony/psc.h>

bool is_sony_psc(uint16_t vid, uint16_t pid) {
    return joypad_is_sony_psc(vid, pid);
}

void process_sony_psc(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    input_event_t event;
    if (!joypad_parse_sony_psc(report, len, &event)) return;
    event.dev_addr = dev_addr;
    event.instance = instance;
    router_submit_input(&event);
}

DeviceInterface sony_psc_interface = {
    .name = "Sony PlayStation Classic",
    .is_device = is_sony_psc,
    .process = process_sony_psc,
    .task = NULL,
    .init = NULL,
};
