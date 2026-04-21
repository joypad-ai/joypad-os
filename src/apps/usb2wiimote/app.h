// app.h - USB2WIIMOTE App Manifest (Pico W)
// USB controllers -> Wiimote-over-Bluetooth-Classic output.
//
// USB HID/XInput controllers come in via PIO-USB host and are forwarded
// as Wiimote HID reports to a paired Wii/Wii U. The Pico W advertises as
// a "Nintendo RVL-CNT-01" so the Wii accepts it as a genuine Wiimote.

#ifndef APP_USB2WIIMOTE_H
#define APP_USB2WIIMOTE_H

#define APP_NAME         "USB2WIIMOTE"
#define APP_VERSION      "0.1.0"
#define APP_DESCRIPTION  "USB controller to Wiimote-over-BT adapter (Pico W)"
#define APP_AUTHOR       "RobertDaleSmith"

// Inputs / outputs
#define REQUIRE_USB_HOST          1
#define MAX_USB_DEVICES           4
#define REQUIRE_BT_OUTPUT         1
#define REQUIRE_USB_DEVICE        1   // CDC config still available

// Services
#define REQUIRE_FLASH_SETTINGS    1
#define REQUIRE_PROFILE_SYSTEM    0
#define REQUIRE_PLAYER_MANAGEMENT 1

// Router
#define ROUTING_MODE     ROUTING_MODE_MERGE
#define MERGE_MODE       MERGE_BLEND
#define APP_MAX_ROUTES   4
#define TRANSFORM_FLAGS  0

// Players (single Wiimote emulation for now — multi-Wiimote would need
// multiple independent BT HID sessions from one stack)
#define PLAYER_SLOT_MODE     PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS     1
#define AUTO_ASSIGN_ON_PRESS 1

// Hardware
#define BOARD              "pico_w"
#define CPU_OVERCLOCK_KHZ  0
#define UART_DEBUG         1

void app_init(void);
void app_task(void);

#endif // APP_USB2WIIMOTE_H
