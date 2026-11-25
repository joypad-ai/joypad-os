// Host stand-in for tusb.h: the real TinyUSB common and HID headers, logging off.
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "common/tusb_common.h"
#include "class/hid/hid.h"
#undef TU_LOG1
#define TU_LOG1(...) ((void)0)
