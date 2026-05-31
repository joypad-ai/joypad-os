// app.h - usb2usb dual: DEVICE/CONSUMER side (A)
// Receives normalized input events from the host side (B) over the inter-MCU
// UART link and presents them as a USB device (all output modes). Sends
// feedback (rumble/LED) back over the link.

#ifndef APP_USB2USB_DUAL_A_H
#define APP_USB2USB_DUAL_A_H

#define APP_NAME "USB2USB-REMAPPER-V7-A"
#define APP_VERSION "0.1.0"
#define APP_DESCRIPTION "Dual-RP2040 device side: UART peer link -> USB device"
#define APP_AUTHOR "RobertDaleSmith"

// Input: inter-MCU UART link only (no local USB host on this MCU)
#define REQUIRE_USB_HOST 0

// Output: USB device
#define REQUIRE_USB_DEVICE 1
#define USB_OUTPUT_PORTS 1

#define REQUIRE_PLAYER_MANAGEMENT 1
#define REQUIRE_FEEDBACK 1

// Routing: UART link -> USB device
#define ROUTING_MODE ROUTING_MODE_MERGE
#define MERGE_MODE MERGE_BLEND
#define TRANSFORM_FLAGS (TRANSFORM_NONE)

#define PLAYER_SLOT_MODE PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS 4
#define AUTO_ASSIGN_ON_PRESS 1

// Inter-MCU UART link (HID-Remapper remapper_v7 board wiring: uart1, crossed A<->B)
#define LINK_UART_INST  1
#define LINK_TX_PIN     24
#define LINK_RX_PIN     25
#define LINK_CTS_PIN    26
#define LINK_RTS_PIN    23
#define LINK_BAUD       4000000
#define LINK_FLOW_CTRL  1

void app_init(void);
void app_task(void);

#endif // APP_USB2USB_DUAL_A_H
