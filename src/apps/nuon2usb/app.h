// app.h - NUON2USB App
// Nuon controller to USB HID gamepad adapter

#ifndef APP_NUON2USB_H
#define APP_NUON2USB_H

#define APP_NAME "NUON2USB"
#define APP_VERSION "0.2.0"
#define APP_DESCRIPTION "Nuon controller reader"
#define APP_AUTHOR "RobertDaleSmith"

// We use nuon device code for PIO programs + protocol constants
#define REQUIRE_NATIVE_NUON_OUTPUT 1
#define NUON_OUTPUT_PORTS 1

#define REQUIRE_USB_HOST 0
#define MAX_USB_DEVICES 0

#define REQUIRE_PLAYER_MANAGEMENT 1

#define ROUTING_MODE ROUTING_MODE_SIMPLE
#define MERGE_MODE MERGE_ALL
#define APP_MAX_ROUTES 1
#define TRANSFORM_FLAGS (TRANSFORM_NONE)

#define PLAYER_SLOT_MODE PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS 1
#define AUTO_ASSIGN_ON_PRESS 0

#define BOARD "ada_kb2040"
#define CPU_OVERCLOCK_KHZ 0
#define UART_DEBUG 1

void app_init(void);
void app_task(void);

#endif // APP_NUON2USB_H
