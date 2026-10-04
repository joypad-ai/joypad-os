// switch2_ble.h - Nintendo Switch 2 Pro Controller output over BLE
// SPDX-License-Identifier: Apache-2.0
//
// BTstack peripheral that a real Switch 2 console pairs with as a native Pro
// Controller 2: Nintendo manufacturer-data advertising, the proprietary GATT table
// (switch2_gatt_db.h), the command channel (switch2_proto), app-level pairing with
// the derived LTK installed for LL encryption (no SMP), reconnect + wake adverts, and
// the input stream. Selected via BLE_MODE_SWITCH2 on builds with
// CONFIG_SWITCH2_BLE_OUTPUT; ble_output dispatches init/late_init/task here.
//
// Radio caveat: the console runs the link at a 5 ms connection interval (below the
// 7.5 ms spec floor). The controller firmware must accept it — see
// .dev/docs/switch2-ble-output-plan.md.

#ifndef SWITCH2_BLE_H
#define SWITCH2_BLE_H

#include <stdbool.h>
#include <stdint.h>

// Pre-BTstack init. Safe before the stack is up.
void switch2_ble_init(void);

// BTstack bring-up (ATT server, advertising, bond restore). Called from
// ble_output_late_init in BTstack context.
void switch2_ble_late_init(void);

// Main-loop task: snapshot the routed controller state; a button press while
// bonded + disconnected switches to the wake advertisement.
void switch2_ble_task(void);

// True while a console is connected (link up, not necessarily streaming yet).
bool switch2_ble_is_connected(void);

// Report what is actually on air: the advertising identity BTstack is using
// (address + whether random addressing is enabled, 0 = public like a real pad)
// and the exact advertising payload. Reading this back over CDC is the only way
// to check the advertisement from a board with no sniffer, and the boot banner
// is printed before USB is up, so it cannot be caught there.
// Returns the advertising payload length.
// Advertise a different controller product id (0 = Pro Controller 2 default).
// Held until changed again -- for trying Joy-Con 2 / NSO GameCube identities.
void switch2_ble_set_pid(uint16_t pid);

// Connection-establishment outcomes: how many LE connection-complete events
// arrived, the last one's status/role/interval, and how many disconnects with
// what reason. A CONNECT_IND the link layer accepted that never becomes a
// connection here shows up as cc_count 0, or as a non-zero cc_status.
void switch2_ble_get_link_debug(uint16_t *cc_count, uint8_t *cc_status,
                                uint8_t *cc_role, uint16_t *cc_itvl,
                                uint16_t *disc_count, uint8_t *disc_reason);

// ATT traffic from the console plus where the 0x15 pairing exchange got to.
// A live link with zero ATT activity means the console connected and never
// discovered our services; activity with pair_stage stuck says which step failed.
// ATT-layer activity: event count and the negotiated MTU. A non-zero MTU proves
// the console opened ATT and spoke to us, which the dynamic-value callbacks
// below cannot show (declarations are served from the static DB).
void switch2_ble_get_att_events(uint16_t *events, uint16_t *mtu);

void switch2_ble_get_att_debug(uint16_t *reads, uint16_t *writes,
                               uint16_t *last_read, uint16_t *last_write,
                               uint8_t *pair_stage, uint8_t *encrypted);

uint8_t switch2_ble_get_adv_debug(uint8_t addr_out[6], uint8_t *addr_mode,
                                  uint8_t *adv_out, uint8_t adv_max);

// Forget the paired console and go back to pairing advertising (the controller's
// sync button). Safe from the main loop — marshalled onto the BTstack run loop.
void switch2_ble_request_sync(void);

#endif // SWITCH2_BLE_H
