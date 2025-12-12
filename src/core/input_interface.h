// input_interface.h
// Input abstraction for Joypad - supports USB host, native, BLE, and UART inputs
//
// Mirrors OutputInterface pattern - apps declare which inputs they use.

#ifndef INPUT_INTERFACE_H
#define INPUT_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include "core/router/router.h"

// Input interface - abstracts different input sources
typedef struct {
    const char* name;                    // Input name (e.g., "USB Host", "SNES", "BLE")
    input_source_t source;               // Router source type for routing table

    void (*init)(void);                  // Initialize input hardware/protocol
    void (*task)(void);                  // Core 0 polling task (NULL if not needed)

    // Status (optional)
    bool (*is_connected)(void);          // Any device connected? (NULL = always true)
    uint8_t (*get_device_count)(void);   // Number of connected devices (NULL = unknown)
} InputInterface;

// Maximum inputs per app (USB host + native + BLE + UART)
#define MAX_INPUT_INTERFACES 4

#endif // INPUT_INTERFACE_H
