// app.h - USB2AMI App Manifest
// USB to Amiga/Atari DE9 adapter

#ifndef APP_USB2AMI_H
#define APP_USB2AMI_H

#define APP_NAME        "USB2AMI"
#define APP_DESCRIPTION "USB to Amiga/Atari DE9 adapter"
#define APP_AUTHOR      "RobertDaleSmith"

// Input drivers
#define REQUIRE_USB_HOST        1
#define MAX_USB_DEVICES         4

// Output drivers
#define REQUIRE_NATIVE_AMIGA_OUTPUT 1

// Services
#define REQUIRE_FLASH_SETTINGS      1
#define REQUIRE_PLAYER_MANAGEMENT   1

// Routing
#define ROUTING_MODE    ROUTING_MODE_MERGE
#define MERGE_MODE      MERGE_BLEND
#define APP_MAX_ROUTES  1
#define TRANSFORM_FLAGS 0

// Player management
#define PLAYER_SLOT_MODE        PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS        1
#define AUTO_ASSIGN_ON_PRESS    1

// Hardware
#define BOARD               "seeed_xiao_rp2040"
#define CPU_OVERCLOCK_KHZ   0
#define UART_DEBUG          0

void app_init(void);
void app_task(void);

#endif // APP_USB2AMI_H
