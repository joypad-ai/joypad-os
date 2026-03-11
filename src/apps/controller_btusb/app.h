// app.h - ControllerBTUSB App Manifest
// Modular sensor inputs → BLE gamepad + USB device output (ESP32-S3)
//
// Same concept as controller (GPIO inputs) but with BLE output like usb2ble.
// First sensor: JoyWing (seesaw I2C). Future: GPIO pads, arcade inputs, etc.

#ifndef APP_CONTROLLER_BTUSB_H
#define APP_CONTROLLER_BTUSB_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "ControllerBTUSB"
#define APP_VERSION "1.0.0"
#define APP_DESCRIPTION "Controller to BLE+USB gamepad adapter"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES
// ============================================================================

// Input drivers
#define REQUIRE_USB_HOST 0
#define REQUIRE_GPIO_INPUT 0

// Output drivers
#ifndef REQUIRE_BLE_OUTPUT
#define REQUIRE_BLE_OUTPUT 1
#endif
#define REQUIRE_USB_DEVICE 1

// Services
#define REQUIRE_FLASH_SETTINGS 1
#define REQUIRE_PROFILE_SYSTEM 0
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_MERGE
#define MERGE_MODE MERGE_BLEND
#define APP_MAX_ROUTES 4

// Input transformations
#define TRANSFORM_FLAGS 0

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS 1
#define AUTO_ASSIGN_ON_PRESS 0

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define CPU_OVERCLOCK_KHZ 0
#define UART_DEBUG 1

// ============================================================================
// BOARD-SPECIFIC CONFIGURATION
// ============================================================================
#if defined(BOARD_FEATHER_RP2040)
    // Adafruit Feather RP2040: JoyWing on STEMMA QT I2C1 (shared with OLED)
    #define JOYWING_I2C_BUS 1
    #define JOYWING_SDA_PIN 2
    #define JOYWING_SCL_PIN 3
#elif defined(BOARD_FEATHER_ESP32S3)
    // Adafruit Feather ESP32-S3: I2C on SDA=3, SCL=4
    #define JOYWING_I2C_BUS 0
    #define JOYWING_SDA_PIN 3
    #define JOYWING_SCL_PIN 4
#elif defined(PLATFORM_ESP32)
    // Generic ESP32-S3 fallback
    #define JOYWING_I2C_BUS 0
    #define JOYWING_SDA_PIN 3
    #define JOYWING_SCL_PIN 4
#endif

// ============================================================================
// APP FEATURES
// ============================================================================
#define FEATURE_PROFILES 0

// ============================================================================
// APP INTERFACE (OS calls these)
// ============================================================================
void app_init(void);
void app_task(void);

#endif // APP_CONTROLLER_BTUSB_H
