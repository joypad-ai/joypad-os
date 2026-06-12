// amiga_device.h - Amiga/CD32 console output driver for JoypadOS
//
// Supports three output platforms:
//   PLATFORM_AMIGA    - CD32 7-button + 2-button joystick + quadrature mouse
//   PLATFORM_C64      - 1-button joystick + quadrature mouse
//   PLATFORM_ATARI_ST - 1-button joystick + Atari ST quadrature mouse
//
// Platform selected via BOOTSEL button (hold 1.5s to cycle).
// DPI divider is per-platform, adjusted via double-click + L/R mouse buttons.
// All settings persisted to flash.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "core/buttons.h"
#include "core/output_interface.h"

// ============================================================================
// PIN DEFINITIONS — set via CMakeLists target_compile_definitions
// ============================================================================

#ifndef AMIGA_PIN_UP
#define AMIGA_PIN_UP        2
#endif
#ifndef AMIGA_PIN_DOWN
#define AMIGA_PIN_DOWN      3
#endif
#ifndef AMIGA_PIN_LEFT
#define AMIGA_PIN_LEFT      4
#endif
#ifndef AMIGA_PIN_RIGHT
#define AMIGA_PIN_RIGHT     5
#endif
#ifndef AMIGA_PIN_CLK
#define AMIGA_PIN_CLK       6
#endif
#ifndef AMIGA_PIN_JOYMODE
#define AMIGA_PIN_JOYMODE   7
#endif
#ifndef AMIGA_PIN_DATA
#define AMIGA_PIN_DATA      8
#endif

#define AMIGA_PIN_FIRE1     AMIGA_PIN_CLK
#define AMIGA_PIN_MMB       AMIGA_PIN_JOYMODE

// ============================================================================
// PLATFORM
// ============================================================================

typedef enum {
    AMIGA_PLATFORM_AMIGA    = 0,
    AMIGA_PLATFORM_C64      = 1,
    AMIGA_PLATFORM_ATARI_ST = 2,
    AMIGA_PLATFORM_COUNT    = 3,
} amiga_platform_t;

// ============================================================================
// OUTPUT MODE
// ============================================================================

typedef enum {
    AMIGA_MODE_JOYSTICK  = 0,
    AMIGA_MODE_CD32      = 1,
    AMIGA_MODE_PLATFORMER = 2,
} amiga_output_mode_t;

// ============================================================================
// DEVICE STATE
// ============================================================================

typedef struct {
    uint32_t           buttons;
    amiga_output_mode_t mode;
} amiga_state_t;

// ============================================================================
// LED COLORS PER PLATFORM
// ============================================================================

// Amiga — warm amber/orange (JoypadOS default)
#define LED_AMIGA_R     25
#define LED_AMIGA_G     8
#define LED_AMIGA_B     0

// C64 — deep blue
#define LED_C64_R       0
#define LED_C64_G       0
#define LED_C64_B       25

// Atari ST — green
#define LED_ATARI_ST_R  0
#define LED_ATARI_ST_G  25
#define LED_ATARI_ST_B  0

// DPI adjustment mode — purple
#define LED_DPI_R       25
#define LED_DPI_G       0
#define LED_DPI_B       25

// ============================================================================
// DPI SETTINGS
// ============================================================================

#define DPI_MIN         1
#define DPI_MAX         8
#define DPI_DEFAULT     2

// ============================================================================
// API
// ============================================================================

void amiga_device_init(void);
void amiga_device_task(void);
void amiga_core1_task(void);

amiga_platform_t amiga_get_platform(void);
uint8_t amiga_get_dpi(void);

extern const OutputInterface amiga_output_interface;
