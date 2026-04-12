// tusb_config_nrf.h - TinyUSB configuration for nRF52840 boards
//
// Device configuration for nRF52840 boards.
// When CONFIG_MAX3421 is defined, adds USB host via MAX3421E FeatherWing.

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

//--------------------------------------------------------------------
// COMMON CONFIGURATION
//--------------------------------------------------------------------

#define CFG_TUSB_MCU                OPT_MCU_NRF5X
#define CFG_TUSB_OS                 OPT_OS_NONE

#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN          __attribute__((aligned(4)))

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG              0
#endif

//--------------------------------------------------------------------
// USB DEVICE CONFIGURATION
//--------------------------------------------------------------------

// RHPORT0 = native nRF52840 USB (always device)
#define CFG_TUSB_RHPORT0_MODE       OPT_MODE_DEVICE

//--------------------------------------------------------------------
// USB HOST CONFIGURATION (MAX3421E FeatherWing on SPI)
//--------------------------------------------------------------------

#ifdef CONFIG_MAX3421
#define CFG_TUSB_RHPORT1_MODE       OPT_MODE_HOST
#define CFG_TUH_MAX3421             1

#define CFG_TUH_ENUMERATION_BUFSIZE 1280
#define CFG_TUH_HUB                 1
#define CFG_TUH_HID                 8
#define CFG_TUH_XINPUT              4
#define CFG_TUH_DEVICE_MAX          (4*CFG_TUH_HUB + 1)
#define CFG_TUH_API_EDPT_XFER       1

#define CFG_TUH_HID_EPIN_BUFSIZE    64
#define CFG_TUH_HID_EPOUT_BUFSIZE   64
#endif // CONFIG_MAX3421

#define CFG_TUD_ENDPOINT0_SIZE      64

// Standard HID gamepad mode
#define CFG_TUD_HID                 4   // Up to 4 HID gamepads

// Xbox Original (XID) mode support
#define CFG_TUD_XID                 1
#define CFG_TUD_XID_EP_BUFSIZE      32

// Xbox 360 (XInput) mode support
#define CFG_TUD_XINPUT              1
#define CFG_TUD_XINPUT_EP_BUFSIZE   32

// GameCube Adapter mode support
#define CFG_TUD_GC_ADAPTER          1
#define CFG_TUD_GC_ADAPTER_EP_BUFSIZE 37

// CDC configuration: single data port
#define CFG_TUD_CDC                 1

#define CFG_TUD_MSC                 0
#define CFG_TUD_MIDI                0
#define CFG_TUD_VENDOR              0

// USB Ethernet (ECM/RNDIS)
#define CFG_TUD_ECM_RNDIS           1
#define CFG_TUD_NCM                 0
#define CFG_TUD_NET_ENDPOINT_SIZE   64
#define CFG_TUD_NET_MTU             1514

// HID buffer sizes
#define CFG_TUD_HID_EP_BUFSIZE      64

// CDC buffer sizes
#define CFG_TUD_CDC_RX_BUFSIZE      256
#define CFG_TUD_CDC_TX_BUFSIZE      1024
#define CFG_TUD_CDC_EP_BUFSIZE      64

#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */
