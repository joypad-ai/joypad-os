// mp_bridge.h - MouthPad NUS <-> USB CDC bridge
//
// Glues the platform-agnostic relay codec (mp_relay) to the two transports:
//   - USB CDC (TinyUSB device, instance 0) <-> the desktop utility
//   - BLE NUS client (btstack_host) <-> the MouthPad
//
// AMBIENT MODEL: the bridge does NOT own the CDC port. It attaches to the shared
// JoypadOS CDC command port via cdc_register_relay() — an RX demux filter that
// claims MouthPad relay frames (0xAA 0x55 ...) and a TX hook that drains NUS
// notifications back out. Every non-relay byte still reaches the JoypadOS command
// parser, and the demux only activates while a MouthPad NUS link is up, so an app
// with no MouthPad connected behaves exactly as before.
//
// Because mp_bridge defines a strong override of cdc.c's weak cdc_relay_late_init(),
// simply LINKING this module into a BLE+CDC app enables the relay automatically
// during cdc_init() — no per-app init/task calls are required.

#ifndef MP_BRIDGE_H
#define MP_BRIDGE_H

#include <stdint.h>

// Attach the relay to the CDC port (registers NUS RX cb + cdc demux/TX hooks).
// Normally invoked automatically via cdc_relay_late_init(); safe to call again.
void mp_bridge_init(void);

// Deprecated no-op: the relay now runs inside cdc_task(). Retained for callers.
void mp_bridge_task(void);

// Lossless-relay stats (any out-pointer may be NULL). drops==0 over a session
// proves zero data loss; high_water is the peak ring occupancy vs ring_size.
void mp_bridge_get_stats(uint32_t* frames, uint32_t* drops,
                         uint32_t* encode_fails, uint32_t* high_water,
                         uint32_t* ring_size);

#endif // MP_BRIDGE_H
