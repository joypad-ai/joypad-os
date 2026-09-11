// app.h - 24G2USB App Manifest
// 8BitDo SF30 2.4G wireless receiver to USB HID gamepad adapter
//
// Drives an nRF24L01+ over SPI, impersonating the 8BitDo SF30 2.4G dongle well
// enough that SF30 2.4G controllers pair and link to it, and presents them to
// the host as a USB gamepad via the existing router -> usbd path.
//
// This manifest is a human-readable summary of what this app uses.
// It is NOT consumed by the build system. The authoritative per-target
// configuration lives in src/CMakeLists.txt. Only #ifndef-guarded flags are
// read by code (see RF24G_PIN_* in native/host/rf24g/rf24g_host.h);
// every other flag here is descriptive only and changing it has no effect.
// See issue #198.

#ifndef APP_24G2USB_H
#define APP_24G2USB_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "24G2USB"
#define APP_DESCRIPTION "8BitDo SF30 2.4G wireless receiver to USB HID gamepad adapter"
#define APP_AUTHOR "FatBeard"

// ============================================================================
// CORE DEPENDENCIES
// ============================================================================

// Input drivers
#define REQUIRE_NATIVE_24G_HOST 1

// Output drivers
#define REQUIRE_USB_DEVICE 1
#define USB_OUTPUT_PORTS 1              // SInput/HID/XInput modes all write to a
                                        // single HID interface regardless of
                                        // player_index -- same as bt2usb.

// Services
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// PIN CONFIGURATION
// ============================================================================
// nRF24L01+ over spi0 (SCK GP6 / TX GP7 / RX GP0). Clear of GP23/24/25/29,
// which the CYW43 module claims on _w boards.
#define RF24G_PIN_MISO 0   // spi0 RX
#define RF24G_PIN_CE   4
#define RF24G_PIN_CSN  5
#define RF24G_PIN_SCK  6   // spi0 SCK
#define RF24G_PIN_MOSI 7   // spi0 TX
#define RF24G_PIN_IRQ  8

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_SIMPLE
#define MERGE_MODE MERGE_ALL

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS 1               // The receiver supports exactly
                                        // one controller, see rf24g_host.h
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "pico2_w"
#define CPU_OVERCLOCK_KHZ 0
#define UART_DEBUG 1

// ============================================================================
// APP INTERFACE
// ============================================================================
void app_init(void);
void app_task(void);

#endif // APP_24G2USB_H
