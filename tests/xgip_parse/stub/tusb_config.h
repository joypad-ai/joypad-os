// Minimal TinyUSB configuration so the real tusb.h and descriptor headers
// build on a PC for this test.
#pragma once
#define CFG_TUSB_MCU    OPT_MCU_NONE
#define CFG_TUSB_OS     OPT_OS_NONE
#define CFG_TUD_ENABLED 1
#define TUP_DCD_ENDPOINT_MAX 8
