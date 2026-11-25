// Minimal TinyUSB config so the real TinyUSB HID headers compile on a PC.
#pragma once
#define CFG_TUSB_MCU OPT_MCU_NONE
#define CFG_TUSB_OS  OPT_OS_NONE
#define CFG_TUH_HID  8
