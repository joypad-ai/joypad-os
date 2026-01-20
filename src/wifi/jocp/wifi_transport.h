// wifi_transport.h - WiFi Transport Layer for JOCP
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Manages CYW43 WiFi in AP mode with LWIP for UDP/TCP networking.
// Receives JOCP packets and dispatches them for processing.

#ifndef WIFI_TRANSPORT_H
#define WIFI_TRANSPORT_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

typedef struct {
    const char* ssid_prefix;    // AP SSID prefix (will append unique suffix)
    const char* password;       // WPA2 password (min 8 chars)
    uint8_t     channel;        // WiFi channel (1-11)
    uint8_t     max_connections;// Max simultaneous connections
    uint16_t    udp_port;       // UDP port for INPUT packets
    uint16_t    tcp_port;       // TCP port for CONTROL channel
} wifi_transport_config_t;

// ============================================================================
// PUBLIC API
// ============================================================================

// Initialize WiFi transport with given configuration
// Returns true on success
bool wifi_transport_init(const wifi_transport_config_t* config);

// Deinitialize WiFi transport
void wifi_transport_deinit(void);

// Process WiFi transport tasks (call from main loop)
// Handles CYW43 polling and LWIP processing
void wifi_transport_task(void);

// Check if WiFi AP is ready and accepting connections
bool wifi_transport_is_ready(void);

// Restart WiFi AP (useful for error recovery)
void wifi_transport_restart(void);

// Get AP SSID (after init)
const char* wifi_transport_get_ssid(void);

// Get AP IP address as string (after init)
const char* wifi_transport_get_ip(void);

// Send data to a specific client (by IP:port)
// Returns bytes sent, or -1 on error
int wifi_transport_send_udp(uint32_t dest_ip, uint16_t dest_port,
                            const uint8_t* data, uint16_t len);

// Send data over TCP to a specific client
// Returns bytes sent, or -1 on error
int wifi_transport_send_tcp(uint32_t client_id, const uint8_t* data, uint16_t len);

#endif // WIFI_TRANSPORT_H
