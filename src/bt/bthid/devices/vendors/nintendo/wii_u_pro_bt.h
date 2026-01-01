// wii_u_pro_bt.h - Nintendo Wii U Pro Controller Bluetooth Driver
#ifndef WII_U_PRO_BT_H
#define WII_U_PRO_BT_H

#include "bt/bthid/bthid.h"

extern const bthid_driver_t wii_u_pro_bt_driver;

void wii_u_pro_bt_register(void);

#endif // WII_U_PRO_BT_H
