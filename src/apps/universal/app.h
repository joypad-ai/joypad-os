// app.h - Universal App Manifest (formerly controller_btusb)
// Modular sensor/GPIO/BT inputs → BLE gamepad + USB device output — the
// universal configurable app: every input source and wireless/USB output
// mode selectable at runtime via web config.
//
// First sensor: JoyWing (seesaw I2C). Future: GPIO pads, arcade inputs, etc.
//
// NOTE: this manifest is a human-readable summary and is NOT consumed by the
// build system. Only the #ifndef-guarded flags below (REQUIRE_BT_INPUT,
// REQUIRE_BLE_OUTPUT) are read by code, and src/CMakeLists.txt overrides them.
// Every other flag here is descriptive only. See issue #198.

#ifndef APP_UNIVERSAL_H
#define APP_UNIVERSAL_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "universal"
#define APP_DESCRIPTION "Controller to BLE+USB gamepad adapter"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES
// ============================================================================

// Input drivers
#define REQUIRE_USB_HOST 0
#define REQUIRE_GPIO_INPUT 0        // Descriptive only, and misleading: GPIO input IS
                                    // built on nRF. nrf/CMakeLists.txt:508 sets
                                    // CONFIG_PAD_INPUT=1 + SENSOR_PAD=1, implemented by
                                    // nrf/src/platform_gpio_nrf.c with pin config from
                                    // PAD.CONFIG.SET (nrf/src/pad_config_storage_nrf.c).
#ifndef REQUIRE_BT_INPUT
#define REQUIRE_BT_INPUT 0          // BLE Central: scan for BT/BLE controllers
#endif
// This app honours the runtime bt_input_enabled flag, so the web-config
// "Enable Bluetooth Host" toggle is a real, user-settable control.
#define BT_INPUT_CONFIGURABLE 1

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
#if defined(BTSTACK_USE_CYW43) || defined(PAD_CONFIG_ABB)
    // Pico W / Pico 2 W / ABB: JoyWing on I2C0 (GP4=SDA, GP5=SCL)
    #define JOYWING_I2C_BUS 0
    #define JOYWING_SDA_PIN 4
    #define JOYWING_SCL_PIN 5
#elif defined(BOARD_FEATHER_RP2040)
    // Adafruit Feather RP2040: JoyWing on STEMMA QT I2C1 (shared with OLED)
    #define JOYWING_I2C_BUS 1
    #define JOYWING_SDA_PIN 2
    #define JOYWING_SCL_PIN 3
#elif defined(BOARD_FEATHER_NRF52840)
    // Adafruit Feather nRF52840: JoyWing on I2C0 (SDA=P0.12, SCL=P0.11)
    // Pins configured via devicetree; values here for debug prints only
    #define JOYWING_I2C_BUS 0
    #define JOYWING_SDA_PIN 12
    #define JOYWING_SCL_PIN 11
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

#endif // APP_UNIVERSAL_H
