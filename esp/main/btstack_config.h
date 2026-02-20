// btstack_config.h wrapper for ESP32 build
// Shadows src/bt/btstack/btstack_config.h (the RP2040 version)
// so BTstack library sources pick up our ESP32 config instead.

#include "../../src/bt/btstack/btstack_config_esp32.h"
