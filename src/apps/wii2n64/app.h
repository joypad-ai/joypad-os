// app.h - WII2N64 App Manifest
// Wii Nunchuck / Classic / Classic Pro to N64 adapter.

#ifndef APP_WII2N64_H
#define APP_WII2N64_H

#define APP_NAME "wii2n64"
#define APP_VERSION "0.1.0"
#define APP_DESCRIPTION "Wii Nunchuck/Classic to N64 adapter"
#define APP_AUTHOR "RobertDaleSmith"

// Input
#define REQUIRE_NATIVE_WII_HOST 1
#define WII_MAX_CONTROLLERS 1

// Output
#define REQUIRE_NATIVE_N64_OUTPUT 1
#define N64_OUTPUT_PORTS 1

// Services
#define REQUIRE_PLAYER_MANAGEMENT 1
#define REQUIRE_PROFILE_SYSTEM 1
#define REQUIRE_FLASH_SETTINGS 1

// Pin configuration — Pi Pico defaults.
//   I2C0 default pair is GP4 (SDA) / GP5 (SCL), free of UART (GP0/1).
//   N64 joybus data on GP2 (matches bt2n64_pico_w convention).
#define WII_PIN_SDA     4
#define WII_PIN_SCL     5
#define WII_I2C_FREQ_HZ 50000

// Routing
#define ROUTING_MODE ROUTING_MODE_SIMPLE
#define MERGE_MODE   MERGE_ALL

// Player management
#define PLAYER_SLOT_MODE     PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS     1
#define AUTO_ASSIGN_ON_PRESS 1

// Hardware
#define BOARD "pico"
#define CPU_OVERCLOCK_KHZ 0
#define UART_DEBUG 1

void app_init(void);
void app_task(void);

#endif // APP_WII2N64_H
