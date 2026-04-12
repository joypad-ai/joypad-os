/*
 * USB Ethernet (ECM/RNDIS) for Joypad-OS
 * Based on GP2040-CE's rndis.c (MIT License, Peter Lawrence)
 */
#ifndef USB_ETHERNET_H_
#define USB_ETHERNET_H_

#include <stdbool.h>
#include <stdint.h>

int usb_ethernet_init(void);
void usb_ethernet_task(void);
bool usb_ethernet_is_active(void);

#endif
