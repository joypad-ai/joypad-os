// app.h - USB2NEOGEO Tournament Edition App Manifest
//
// Supports multiple configurations via compile definitions:
//   CONFIG_NEOGEO_TE_2P=1  -- 2-player Pico build (Native USB = P1, PIO USB = P2)
//   Default                -- 1-player RP2040-Zero/KB2040/Pico build

#ifndef APP_USB2NEOGEO_TE_H
#define APP_USB2NEOGEO_TE_H

// ============================================================================
// APP METADATA
// ============================================================================
#ifdef CONFIG_NEOGEO_TE_2P
#define APP_NAME        "USB2NEOGEO_TE_2P"
#define APP_DESCRIPTION "USB to NEOGEO adapter - Tournament Edition 2-Player"
#else
#define APP_NAME        "USB2NEOGEO_TE"
#define APP_DESCRIPTION "USB to NEOGEO adapter - Tournament Edition"
#endif
#define APP_AUTHOR "originalgrego"

// ============================================================================
// CORE DEPENDENCIES
// ============================================================================

#define REQUIRE_USB_HOST 1
#define REQUIRE_NATIVE_NEOGEO_OUTPUT 1
#define REQUIRE_FLASH_SETTINGS 1
#define REQUIRE_PROFILE_SYSTEM 1
#define REQUIRE_PLAYER_MANAGEMENT 1

#ifdef CONFIG_NEOGEO_TE_2P
    #define MAX_USB_DEVICES     2
    #define NEOGEO_OUTPUT_PORTS 2
#else
    #define MAX_USB_DEVICES     1
    #define NEOGEO_OUTPUT_PORTS 1
#endif

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE    ROUTING_MODE_SIMPLE
#define MERGE_MODE      MERGE_ALL
#define TRANSFORM_FLAGS (TRANSFORM_NONE)

#ifdef CONFIG_NEOGEO_TE_2P
    #define MAX_ROUTES 2
#else
    #define MAX_ROUTES 1
#endif

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#ifdef CONFIG_NEOGEO_TE_2P
    // Fixed: port 1 always P1, port 2 always P2, no shifting on disconnect
    #define PLAYER_SLOT_MODE     PLAYER_SLOT_FIXED
    #define MAX_PLAYER_SLOTS     2
    #define AUTO_ASSIGN_ON_PRESS 0
#else
    #define PLAYER_SLOT_MODE     PLAYER_SLOT_SHIFT
    #define MAX_PLAYER_SLOTS     1
    #define AUTO_ASSIGN_ON_PRESS 1
#endif

// ============================================================================
// PLAYER 1 GPIO PINS
// ============================================================================
#ifdef CONFIG_NEOGEO_TE_2P
    // 2-player Pico layout
    #define P1_NEOGEO_DU_PIN  2
    #define P1_NEOGEO_DD_PIN  3
    #define P1_NEOGEO_DL_PIN  4
    #define P1_NEOGEO_DR_PIN  5
    #define P1_NEOGEO_S1_PIN  6
    #define P1_NEOGEO_S2_PIN  7
    #define P1_NEOGEO_B1_PIN  8
    #define P1_NEOGEO_B2_PIN  9
    #define P1_NEOGEO_B3_PIN  10
    #define P1_NEOGEO_B4_PIN  11
    #define P1_NEOGEO_B5_PIN  12
    #define P1_NEOGEO_B6_PIN  13
#elif defined(RPI_PICO_BUILD)
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
// PLAYER 2 GPIO PINS (2-player Pico build only)
// ============================================================================
#ifdef CONFIG_NEOGEO_TE_2P
    #define P2_NEOGEO_DU_PIN  18
    #define P2_NEOGEO_DD_PIN  19
    #define P2_NEOGEO_DL_PIN  20
    #define P2_NEOGEO_DR_PIN  21
    #define P2_NEOGEO_S1_PIN  22
    #define P2_NEOGEO_S2_PIN  26
    #define P2_NEOGEO_B1_PIN  27
    #define P2_NEOGEO_B2_PIN  28
    #define P2_NEOGEO_B3_PIN  0
    #define P2_NEOGEO_B4_PIN  1
    #define P2_NEOGEO_B5_PIN  14
    #define P2_NEOGEO_B6_PIN  15
    // PIO USB D+ pin for Player 2 input (D- is implicitly D+ + 1 = GP17)
    #define PICO_PIO_USB_DP_PIN 16
#endif

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#ifdef CONFIG_NEOGEO_TE_2P
    #define BOARD "pico_2p"
#elif defined(RPI_PICO_BUILD)
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

