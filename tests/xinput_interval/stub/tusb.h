// Minimal stand-in for tusb.h so xinput_descriptors.h compiles on a PC.
#ifndef TEST_STUB_TUSB_H
#define TEST_STUB_TUSB_H
#include <stdint.h>
#include "common/tusb_compiler.h"
#include "common/tusb_types.h"
#ifndef U16_TO_U8S_LE
#define U16_TO_U8S_LE(_u16) ((uint8_t)((_u16) & 0xff)), ((uint8_t)(((_u16) >> 8) & 0xff))
#endif
#endif
