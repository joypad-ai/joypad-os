// cdc_commands.h - CDC command handlers
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#ifndef CDC_COMMANDS_H
#define CDC_COMMANDS_H

#include "cdc_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize command handlers
void cdc_commands_init(void);

// Task function - call periodically to handle auto-stop rumble, etc.
void cdc_commands_task(void);

// Process a received command packet
// Called by protocol layer when valid CMD packet received
void cdc_commands_process(const cdc_packet_t* packet);

// Get protocol context (for sending events)
cdc_protocol_t* cdc_commands_get_protocol(void);

// Set active protocol context for command responses (used by NUS bridge).
// Pass NULL to restore the default USB CDC context.
void cdc_commands_set_active_protocol(cdc_protocol_t* ctx);

// Send input event (if streaming enabled) - raw input before profile mapping
void cdc_commands_send_input_event(uint32_t buttons, const uint8_t* axes);

// Send output event (if streaming enabled) - processed output after profile mapping
void cdc_commands_send_output_event(uint32_t buttons, const uint8_t* axes);

// Per-player input event with source identification
void cdc_commands_send_player_input(uint8_t player, uint8_t dev_addr,
                                    const char* name, const char* source,
                                    uint32_t buttons, const uint8_t* axes);

// Per-player output event (merged result)
void cdc_commands_send_player_output(uint8_t player, uint32_t buttons,
                                     const uint8_t* axes);

// Send controller connect/disconnect event
void cdc_commands_send_connect_event(uint8_t port, const char* name,
                                     uint16_t vid, uint16_t pid);
void cdc_commands_send_disconnect_event(uint8_t port);

#ifdef __cplusplus
}
#endif

#endif // CDC_COMMANDS_H
