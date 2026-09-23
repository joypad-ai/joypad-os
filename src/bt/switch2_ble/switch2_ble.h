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

// Forget the paired console and go back to pairing advertising (the controller's
// sync button). Safe from the main loop — marshalled onto the BTstack run loop.
void switch2_ble_request_sync(void);

#endif // SWITCH2_BLE_H
