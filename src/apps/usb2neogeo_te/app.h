// app.h - USB2NEOGEO Tournament Mode App Manifest
//
// Stripped-down usb2neogeo build for tournament use:
//   - Single profile (default 1L6B layout), no profile switching
//   - Consecutive button remap only (SELECT held 2s, then press 6 buttons)
//   - Remap auto-clears on controller disconnect
//   - No alternative remap mode (no multi-button-to-one-output)
//   - No auto fire

#ifndef APP_USB2NEOGEO_TE_H
#define APP_USB2NEOGEO_TE_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "USB2NEOGEO_TE"
#define APP_DESCRIPTION "USB to NEOGEO adapter - Tournament Mode"
#define APP_AUTHOR "originalgrego"

// ============================================================================
// CORE DEPENDENCIES
// ============================================================================

#define REQUIRE_USB_HOST 1
#define MAX_USB_DEVICES 1

#define REQUIRE_NATIVE_NEOGEO_OUTPUT 1
#define NEOGEO_OUTPUT_PORTS 1

#define REQUIRE_FLASH_SETTINGS 1
#define REQUIRE_PROFILE_SYSTEM 1
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_SIMPLE
#define MERGE_MODE MERGE_ALL
#define MAX_ROUTES 1
#define TRANSFORM_FLAGS (TRANSFORM_NONE)

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_SHIFT
#define MAX_PLAYER_SLOTS 1
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// PLAYER GPIO PINS (identical to usb2neogeo)
// ============================================================================
#ifdef RPI_PICO_BUILD
    #define P1_NEOGEO_DU_PIN 19
    #define P1_NEOGEO_DD_PIN 2
    #define P1_NEOGEO_DR_PIN 3
    #define P1_NEOGEO_DL_PIN 28
    #define P1_NEOGEO_S1_PIN 6
    #define P1_NEOGEO_S2_PIN 18
    #define P1_NEOGEO_B1_PIN 27
    #define P1_NEOGEO_B2_PIN 4
    #define P1_NEOGEO_B3_PIN 26
    #define P1_NEOGEO_B4_PIN 5
    #define P1_NEOGEO_B5_PIN 20
    #define P1_NEOGEO_B6_PIN 7
#elif defined(PICO_RP2040_ZERO_BUILD)
    #define P1_NEOGEO_DU_PIN 14
    #define P1_NEOGEO_DD_PIN 29
    #define P1_NEOGEO_DR_PIN 28
    #define P1_NEOGEO_DL_PIN 13
    #define P1_NEOGEO_S1_PIN 3
    #define P1_NEOGEO_S2_PIN 10
    #define P1_NEOGEO_B1_PIN 12
    #define P1_NEOGEO_B2_PIN 27
    #define P1_NEOGEO_B3_PIN 11
    #define P1_NEOGEO_B4_PIN 4
    #define P1_NEOGEO_B5_PIN 9
    #define P1_NEOGEO_B6_PIN 2
#else
    #define P1_NEOGEO_DU_PIN 29
    #define P1_NEOGEO_DD_PIN 2
    #define P1_NEOGEO_DR_PIN 3
    #define P1_NEOGEO_DL_PIN 28
    #define P1_NEOGEO_S1_PIN 6
    #define P1_NEOGEO_S2_PIN 18
    #define P1_NEOGEO_B1_PIN 27
    #define P1_NEOGEO_B2_PIN 4
    #define P1_NEOGEO_B3_PIN 26
    #define P1_NEOGEO_B4_PIN 5
    #define P1_NEOGEO_B5_PIN 20
    #define P1_NEOGEO_B6_PIN 7
#endif

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#ifdef RPI_PICO_BUILD
    #define BOARD "pico"
#elif defined(PICO_RP2040_ZERO_BUILD)
    #define BOARD "rp2040zero"
#else
    #define BOARD "ada_kb2040"
#endif
#define CPU_OVERCLOCK_KHZ 0
#define UART_DEBUG 1

// ============================================================================
// APP FEATURES
// ============================================================================
#define FEATURE_PROFILES 1

// ============================================================================
// APP INTERFACE
// ============================================================================
void app_init(void);
void app_task(void);

#endif // APP_USB2NEOGEO_TE_H
