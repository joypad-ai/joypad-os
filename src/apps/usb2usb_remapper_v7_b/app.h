// app.h - usb2usb dual: HOST/PRODUCER side (B)
// Reads USB controllers (via the on-board hub) on the native USB host and
// pushes normalized input events over the inter-MCU UART link to the device
// side (A). Receives feedback (rumble/LED) back over the link.

#ifndef APP_USB2USB_DUAL_B_H
#define APP_USB2USB_DUAL_B_H

#define APP_NAME "USB2USB-REMAPPER-V7-B"
#define APP_VERSION "0.1.0"
#define APP_DESCRIPTION "Dual-RP2040 host side: USB host + hub -> UART peer link"
#define APP_AUTHOR "RobertDaleSmith"

// Input: native USB host (+ hub, configured in tusb_config.h)
#define REQUIRE_USB_HOST 1
#define MAX_USB_DEVICES 6

// Output: UART peer link (feedback per player)
#define REQUIRE_UART_OUTPUT 1
#define UART_OUTPUT_PLAYERS 8

#define REQUIRE_PLAYER_MANAGEMENT 1
#define REQUIRE_FEEDBACK 1

// Routing: USB host -> UART link tap
#define ROUTING_MODE ROUTING_MODE_SIMPLE
#define MERGE_MODE MERGE_PRIORITY
#define TRANSFORM_FLAGS (TRANSFORM_NONE)

#define PLAYER_SLOT_MODE PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS 8
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

#endif // APP_USB2USB_DUAL_B_H
