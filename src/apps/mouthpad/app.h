// app.h - MouthPad App Manifest
//
// Augmental MouthPad bridge: BLE in (HID + Nordic UART Service) -> USB out.
//
// - HID: the MouthPad's mouse/keyboard reports flow through the JoypadOS
//   router/profile pipeline (via the mouthpad_ble bthid driver) and out the
//   SInput composite (mouse + keyboard + gamepad), so the MouthPad is a
//   first-class, remappable, combinable input device.
// - NUS: the MouthPad's custom Nordic UART Service stream is bridged over USB
//   CDC using the PacketFramer + relay protobuf the Augmental desktop utility
//   speaks, so the app keeps full access as if connected directly.
//
// Runs on the April Brother nRF52840 dongle and any Pico W / Pico 2 W.

#ifndef APP_MOUTHPAD_H
#define APP_MOUTHPAD_H

#define APP_NAME "MOUTHPAD"
#define APP_DESCRIPTION "Augmental MouthPad BLE -> USB (HID + NUS relay)"
#define APP_AUTHOR "RobertDaleSmith"

// Input: BLE central (CYW43 on Pico W / Zephyr HCI on nRF). MouthPad is BLE.
#define REQUIRE_BT_CYW43 1
#define REQUIRE_USB_HOST 0
#define MAX_USB_DEVICES 0

// Output: USB device (SInput composite is the default output mode).
#define REQUIRE_USB_DEVICE 1
#define USB_OUTPUT_PORTS 1

#define REQUIRE_FLASH_SETTINGS 0
#define REQUIRE_PROFILE_SYSTEM 0
#define REQUIRE_PLAYER_MANAGEMENT 1

#define ROUTING_MODE ROUTING_MODE_MERGE
#define MERGE_MODE MERGE_BLEND
#define APP_MAX_ROUTES 4
#define TRANSFORM_FLAGS 0

#define PLAYER_SLOT_MODE PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS 4
#define AUTO_ASSIGN_ON_PRESS 1

#define BOARD "aprbrother_nrf52840"
#define CPU_OVERCLOCK_KHZ 0
#define UART_DEBUG 1

#define BT_MAX_CONNECTIONS 4
#define BT_SCAN_ON_STARTUP 1

void app_init(void);
void app_task(void);

#endif // APP_MOUTHPAD_H
