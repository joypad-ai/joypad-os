// Host-test TinyUSB config: device stack, HID only, no MCU.
#ifndef KBM_TEST_TUSB_CONFIG_H
#define KBM_TEST_TUSB_CONFIG_H
#define CFG_TUSB_MCU            OPT_MCU_NONE
#define CFG_TUSB_OS             OPT_OS_NONE
#define CFG_TUD_ENABLED         1
#define CFG_TUD_ENDPOINT0_SIZE  64
#define CFG_TUD_HID             3
#define CFG_TUD_HID_EP_BUFSIZE  64
#endif
