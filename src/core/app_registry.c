// app_registry.c
// Shared registry of the active app's input/output interfaces.

#include "core/app_registry.h"
#include <stddef.h>

static const InputInterface* const* s_inputs = NULL;
static uint8_t s_input_count = 0;
static const OutputInterface* const* s_outputs = NULL;
static uint8_t s_output_count = 0;

void app_registry_set(const InputInterface* const* inputs, uint8_t input_count,
                      const OutputInterface* const* outputs, uint8_t output_count)
{
    s_inputs = inputs;
    s_input_count = inputs ? input_count : 0;
    s_outputs = outputs;
    s_output_count = outputs ? output_count : 0;
}

const InputInterface* const* app_registry_inputs(uint8_t* count)
{
    if (count) *count = s_input_count;
    return s_inputs;
}

const OutputInterface* const* app_registry_outputs(uint8_t* count)
{
    if (count) *count = s_output_count;
    return s_outputs;
}

const char* app_registry_input_source_name(input_source_t source)
{
    switch (source) {
        case INPUT_SOURCE_USB_HOST:        return "usb_host";
        case INPUT_SOURCE_BLE_CENTRAL:     return "ble_central";
        case INPUT_SOURCE_WIFI:            return "wifi";
        case INPUT_SOURCE_NATIVE_NES:      return "native_nes";
        case INPUT_SOURCE_NATIVE_SNES:     return "native_snes";
        case INPUT_SOURCE_NATIVE_N64:      return "native_n64";
        case INPUT_SOURCE_NATIVE_GC:       return "native_gc";
        case INPUT_SOURCE_NATIVE_3DO:      return "native_3do";
        case INPUT_SOURCE_NATIVE_ARCADE:   return "native_arcade";
        case INPUT_SOURCE_NATIVE_JVS:      return "native_jvs";
        case INPUT_SOURCE_NATIVE_LODGENET: return "native_lodgenet";
        case INPUT_SOURCE_NATIVE_NUON:     return "native_nuon";
        case INPUT_SOURCE_NATIVE_WII:      return "native_wii";
        case INPUT_SOURCE_NATIVE_PSX:      return "native_psx";
        case INPUT_SOURCE_GPIO:            return "gpio";
        case INPUT_SOURCE_SENSORS:         return "sensors";
        case INPUT_SOURCE_I2C_PEER:        return "i2c_peer";
        case INPUT_SOURCE_UART_PEER:       return "uart_peer";
    }
    return "unknown";
}

const char* app_registry_output_target_name(output_target_t target)
{
    switch (target) {
        case OUTPUT_TARGET_NONE:           return "none";
        case OUTPUT_TARGET_GAMECUBE:       return "gamecube";
        case OUTPUT_TARGET_PCENGINE:       return "pcengine";
        case OUTPUT_TARGET_3DO:            return "3do";
        case OUTPUT_TARGET_NUON:           return "nuon";
        case OUTPUT_TARGET_XBOXONE:        return "xboxone";
        case OUTPUT_TARGET_LOOPY:          return "loopy";
        case OUTPUT_TARGET_DREAMCAST:      return "dreamcast";
        case OUTPUT_TARGET_N64:            return "n64";
        case OUTPUT_TARGET_GPIO:           return "gpio";
        case OUTPUT_TARGET_USB_DEVICE:     return "usb_device";
        case OUTPUT_TARGET_BLE_PERIPHERAL: return "ble_peripheral";
        case OUTPUT_TARGET_UART:           return "uart";
        case OUTPUT_TARGET_WII_EXTENSION:  return "wii_extension";
        case OUTPUT_TARGET_COUNT:          break;
    }
    return "unknown";
}

const char* app_registry_routing_mode_name(uint8_t mode)
{
    switch ((routing_mode_t)mode) {
        case ROUTING_MODE_SIMPLE:       return "simple";
        case ROUTING_MODE_MERGE:        return "merge";
        case ROUTING_MODE_BROADCAST:    return "broadcast";
        case ROUTING_MODE_CONFIGURABLE: return "configurable";
    }
    return "unknown";
}

const char* app_registry_merge_mode_name(uint8_t mode)
{
    switch ((merge_mode_t)mode) {
        case MERGE_PRIORITY: return "priority";
        case MERGE_BLEND:    return "blend";
        case MERGE_ALL:      return "all";
    }
    return "unknown";
}
