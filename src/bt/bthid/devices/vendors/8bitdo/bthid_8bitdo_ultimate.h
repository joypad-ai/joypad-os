// bthid_8bitdo_ultimate.h - 8BitDo Ultimate 3-mode Xbox Controller (BLE)

#pragma once

#include "bt/bthid/bthid.h"

extern const bthid_driver_t bthid_8bitdo_ultimate_driver;

void bthid_8bitdo_ultimate_register(void);
