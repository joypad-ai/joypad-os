// battery.h - Shared battery helpers
//
// Single-cell LiPo voltage → percentage. Rough piecewise-linear curve, good
// enough for an on-screen indicator; not a coulomb-counting fuel gauge.

#ifndef CORE_BATTERY_H
#define CORE_BATTERY_H

#include <stdint.h>

// Map a single-cell LiPo terminal voltage (millivolts) to 0-100 percent.
static inline uint8_t battery_percent_from_mv(int mv)
{
    if (mv >= 4200)  return 100;
    if (mv <= 3300)  return 0;
    if (mv >= 4000)  return (uint8_t)(80 + (mv - 4000) * 20 / 200);  // 4.0-4.2V
    if (mv >= 3700)  return (uint8_t)(40 + (mv - 3700) * 40 / 300);  // 3.7-4.0V
    if (mv >= 3500)  return (uint8_t)(15 + (mv - 3500) * 25 / 200);  // 3.5-3.7V
    return (uint8_t)((mv - 3300) * 15 / 200);                        // 3.3-3.5V
}

#endif // CORE_BATTERY_H
