// pad_input_stub.c - Stub for pad_input on ESP32
// GPIO pad polling not yet ported to ESP-IDF. Only config storage works.

#include "pad/pad_input.h"

static uint8_t stub_count = 0;

int pad_input_add_device(const pad_device_config_t* config) {
    (void)config;
    return -1;  // Not supported yet
}

void pad_input_clear_devices(void) { stub_count = 0; }
uint8_t pad_input_get_device_count(void) { return stub_count; }
const input_event_t* pad_input_get_event(uint8_t device_index) { (void)device_index; return NULL; }
const pad_device_config_t* pad_input_get_config(uint8_t device_index) { (void)device_index; return NULL; }
