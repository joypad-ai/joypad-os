// output_interface.h
// Output abstraction for USBRetro - supports native console and USB device outputs

#ifndef OUTPUT_INTERFACE_H
#define OUTPUT_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include "core/input_event.h"

// Output interface - abstracts different output types (native consoles, USB device, BLE, etc.)
typedef struct {
    const char* name;                                      // Output name (e.g., "GameCube", "USB Device (XInput)")

    void (*init)(void);                                    // Initialize output hardware/protocol
    void (*handle_input)(const input_event_t* event);      // Handle incoming input event
    void (*core1_entry)(void);                             // Core1 entry point (NULL if not needed)
    void (*task)(void);                                    // Periodic task (NULL if not needed)
} OutputInterface;

// Active output interface (set at compile-time, selected in common/output.c)
extern const OutputInterface* active_output;

#endif // OUTPUT_INTERFACE_H
