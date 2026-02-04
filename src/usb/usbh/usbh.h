// usbh.h - USB Host Layer
//
// Provides unified USB host handling across HID and X-input protocols.
// Abstracts feedback (rumble, LEDs, triggers) delivery to all USB input devices.

#ifndef USBH_H
#define USBH_H

#include <stdint.h>
#include <stdbool.h>
#include "core/input_interface.h"

// Initialize USB host layer (HID registry, etc.)
void usbh_init(void);

// Process USB host feedback tasks (rumble, LEDs, trigger thresholds)
// Combines console feedback with profile indicator feedback internally
void usbh_task(void);

// Signal that BT hardware is available (USB dongle detected or Pico W onboard)
// When false, BTstack loop processing is skipped to reduce latency
void usbh_set_bt_available(bool available);

// USB host input interface (implements InputInterface pattern)
extern const InputInterface usbh_input_interface;

#endif // USBH_H
