// wifi_transport.c - WiFi Transport Layer for JOCP
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// CYW43 WiFi AP mode with LWIP UDP/TCP servers for JOCP protocol.

#include "wifi_transport.h"
#include "jocp.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/unique_id.h"

#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "dhcpserver.h"

#include <stdio.h>
#include <string.h>

// ============================================================================
// STATE
// ============================================================================

static wifi_transport_config_t config;
static bool initialized = false;
static bool ap_ready = false;

// Network state
static struct udp_pcb* udp_pcb = NULL;
static struct tcp_pcb* tcp_listen_pcb = NULL;
static char ap_ssid[32];
static char ap_ip_str[16];

// DHCP server state
static dhcp_server_t dhcp_server;

// TCP client tracking (for control channel)
#define MAX_TCP_CLIENTS 4
typedef struct {
    struct tcp_pcb* pcb;
    uint32_t ip;
    uint16_t port;
    bool connected;
} tcp_client_t;
static tcp_client_t tcp_clients[MAX_TCP_CLIENTS];

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

static void udp_recv_callback(void* arg, struct udp_pcb* pcb, struct pbuf* p,
                              const ip_addr_t* addr, u16_t port);
static err_t tcp_accept_callback(void* arg, struct tcp_pcb* newpcb, err_t err);
static err_t tcp_recv_callback(void* arg, struct tcp_pcb* tpcb, struct pbuf* p, err_t err);
static void tcp_err_callback(void* arg, err_t err);

// ============================================================================
// INITIALIZATION
// ============================================================================

bool wifi_transport_init(const wifi_transport_config_t* cfg)
{
    if (initialized) {
        printf("[wifi] Already initialized\n");
        return true;
    }

    // Save config
    memcpy(&config, cfg, sizeof(config));

    // Initialize CYW43 with country code
    printf("[wifi] Initializing CYW43...\n");
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA)) {
        printf("[wifi] ERROR: Failed to initialize CYW43\n");
        return false;
    }

    // Generate unique SSID from board ID
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);
    snprintf(ap_ssid, sizeof(ap_ssid), "%s%02X%02X",
             config.ssid_prefix,
             board_id.id[6], board_id.id[7]);

    printf("[wifi] Starting AP: %s\n", ap_ssid);
    printf("[wifi] Password: %s\n", config.password);
    printf("[wifi] Channel: %d\n", config.channel);

    // Enable AP mode
    cyw43_arch_enable_ap_mode(ap_ssid, config.password, CYW43_AUTH_WPA2_AES_PSK);

    // Configure static IP for AP (192.168.4.1)
    ip4_addr_t gw, mask;
    IP4_ADDR(&gw, 192, 168, 4, 1);
    IP4_ADDR(&mask, 255, 255, 255, 0);

    // Set netif IP address
    netif_set_addr(netif_default, &gw, &mask, &gw);

    // Store IP as string
    snprintf(ap_ip_str, sizeof(ap_ip_str), "%s", ip4addr_ntoa(&gw));
    printf("[wifi] AP IP: %s\n", ap_ip_str);

    // Start DHCP server (gives clients 192.168.4.2 - 192.168.4.5)
    ip4_addr_t dhcp_start;
    IP4_ADDR(&dhcp_start, 192, 168, 4, 2);
    dhcp_server_init(&dhcp_server, &gw, &dhcp_start);
    printf("[wifi] DHCP server started\n");

    // Create UDP socket for JOCP INPUT packets
    printf("[wifi] Creating UDP server on port %d...\n", config.udp_port);
    udp_pcb = udp_new();
    if (!udp_pcb) {
        printf("[wifi] ERROR: Failed to create UDP PCB\n");
        return false;
    }

    err_t err = udp_bind(udp_pcb, IP_ADDR_ANY, config.udp_port);
    if (err != ERR_OK) {
        printf("[wifi] ERROR: Failed to bind UDP port %d: %d\n", config.udp_port, err);
        udp_remove(udp_pcb);
        udp_pcb = NULL;
        return false;
    }

    udp_recv(udp_pcb, udp_recv_callback, NULL);
    printf("[wifi] UDP server listening on port %d\n", config.udp_port);

    // Create TCP socket for JOCP CONTROL channel
    printf("[wifi] Creating TCP server on port %d...\n", config.tcp_port);
    tcp_listen_pcb = tcp_new();
    if (!tcp_listen_pcb) {
        printf("[wifi] ERROR: Failed to create TCP PCB\n");
        return false;
    }

    err = tcp_bind(tcp_listen_pcb, IP_ADDR_ANY, config.tcp_port);
    if (err != ERR_OK) {
        printf("[wifi] ERROR: Failed to bind TCP port %d: %d\n", config.tcp_port, err);
        tcp_close(tcp_listen_pcb);
        tcp_listen_pcb = NULL;
        return false;
    }

    tcp_listen_pcb = tcp_listen(tcp_listen_pcb);
    tcp_accept(tcp_listen_pcb, tcp_accept_callback);
    printf("[wifi] TCP server listening on port %d\n", config.tcp_port);

    // Clear TCP client tracking
    memset(tcp_clients, 0, sizeof(tcp_clients));

    // Initialize JOCP subsystem
    jocp_init();

    initialized = true;
    ap_ready = true;

    printf("[wifi] WiFi transport initialized\n");
    printf("[wifi] Connect to SSID: %s\n", ap_ssid);
    printf("[wifi] Then send JOCP packets to %s:%d\n", ap_ip_str, config.udp_port);

    return true;
}

void wifi_transport_deinit(void)
{
    if (!initialized) return;

    ap_ready = false;

    // Close UDP socket
    if (udp_pcb) {
        udp_remove(udp_pcb);
        udp_pcb = NULL;
    }

    // Close TCP server
    if (tcp_listen_pcb) {
        tcp_close(tcp_listen_pcb);
        tcp_listen_pcb = NULL;
    }

    // Close TCP clients
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
        if (tcp_clients[i].connected && tcp_clients[i].pcb) {
            tcp_close(tcp_clients[i].pcb);
        }
    }
    memset(tcp_clients, 0, sizeof(tcp_clients));

    // Stop DHCP server
    dhcp_server_deinit(&dhcp_server);

    // Deinitialize CYW43
    cyw43_arch_deinit();

    initialized = false;
    printf("[wifi] WiFi transport deinitialized\n");
}

// ============================================================================
// TASK PROCESSING
// ============================================================================

void wifi_transport_task(void)
{
    if (!initialized) return;

    // Poll CYW43 and process LWIP
    cyw43_arch_poll();
}

// ============================================================================
// STATUS QUERIES
// ============================================================================

bool wifi_transport_is_ready(void)
{
    return ap_ready;
}

void wifi_transport_restart(void)
{
    printf("[wifi] Restarting WiFi AP...\n");
    wifi_transport_deinit();
    sleep_ms(500);
    wifi_transport_init(&config);
}

const char* wifi_transport_get_ssid(void)
{
    return ap_ssid;
}

const char* wifi_transport_get_ip(void)
{
    return ap_ip_str;
}

// ============================================================================
// UDP HANDLING
// ============================================================================

static void udp_recv_callback(void* arg, struct udp_pcb* pcb, struct pbuf* p,
                              const ip_addr_t* addr, u16_t port)
{
    (void)arg;
    (void)pcb;

    if (!p) return;

    // Copy packet data (pbuf may be chained)
    uint8_t buffer[128];
    uint16_t len = pbuf_copy_partial(p, buffer, sizeof(buffer), 0);

    // Process JOCP packet
    uint32_t src_ip = ip4_addr_get_u32(ip_2_ip4(addr));
    jocp_process_input_packet(buffer, len, src_ip, port);

    // Free pbuf
    pbuf_free(p);
}

int wifi_transport_send_udp(uint32_t dest_ip, uint16_t dest_port,
                            const uint8_t* data, uint16_t len)
{
    if (!udp_pcb || !ap_ready) return -1;

    struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    if (!p) return -1;

    memcpy(p->payload, data, len);

    ip_addr_t addr;
    ip4_addr_set_u32(ip_2_ip4(&addr), dest_ip);
    IP_SET_TYPE(&addr, IPADDR_TYPE_V4);

    err_t err = udp_sendto(udp_pcb, p, &addr, dest_port);
    pbuf_free(p);

    return (err == ERR_OK) ? len : -1;
}

// ============================================================================
// TCP HANDLING
// ============================================================================

static int find_free_tcp_slot(void)
{
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
        if (!tcp_clients[i].connected) return i;
    }
    return -1;
}

static int find_tcp_client_by_pcb(struct tcp_pcb* pcb)
{
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
        if (tcp_clients[i].connected && tcp_clients[i].pcb == pcb) return i;
    }
    return -1;
}

static err_t tcp_accept_callback(void* arg, struct tcp_pcb* newpcb, err_t err)
{
    (void)arg;

    if (err != ERR_OK || !newpcb) {
        return ERR_VAL;
    }

    int slot = find_free_tcp_slot();
    if (slot < 0) {
        printf("[wifi] TCP: Max clients reached, rejecting connection\n");
        tcp_abort(newpcb);
        return ERR_ABRT;
    }

    printf("[wifi] TCP: Client connected from %s:%d (slot %d)\n",
           ip4addr_ntoa(&newpcb->remote_ip), newpcb->remote_port, slot);

    tcp_clients[slot].pcb = newpcb;
    tcp_clients[slot].ip = ip4_addr_get_u32(&newpcb->remote_ip);
    tcp_clients[slot].port = newpcb->remote_port;
    tcp_clients[slot].connected = true;

    tcp_arg(newpcb, (void*)(intptr_t)slot);
    tcp_recv(newpcb, tcp_recv_callback);
    tcp_err(newpcb, tcp_err_callback);

    return ERR_OK;
}

static err_t tcp_recv_callback(void* arg, struct tcp_pcb* tpcb, struct pbuf* p, err_t err)
{
    int slot = (int)(intptr_t)arg;

    if (!p || err != ERR_OK) {
        // Connection closed
        if (slot >= 0 && slot < MAX_TCP_CLIENTS) {
            printf("[wifi] TCP: Client disconnected (slot %d)\n", slot);
            tcp_clients[slot].connected = false;
            tcp_clients[slot].pcb = NULL;
        }
        if (tpcb) tcp_close(tpcb);
        return ERR_OK;
    }

    // Copy packet data
    uint8_t buffer[256];
    uint16_t len = pbuf_copy_partial(p, buffer, sizeof(buffer), 0);

    // Acknowledge received data
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);

    // TODO: Process TCP control messages (CAPS_REQ, OUTPUT_CMD, etc.)
    printf("[wifi] TCP: Received %d bytes from slot %d\n", len, slot);

    return ERR_OK;
}

static void tcp_err_callback(void* arg, err_t err)
{
    int slot = (int)(intptr_t)arg;

    printf("[wifi] TCP: Error %d on slot %d\n", err, slot);

    if (slot >= 0 && slot < MAX_TCP_CLIENTS) {
        tcp_clients[slot].connected = false;
        tcp_clients[slot].pcb = NULL;
    }
}

int wifi_transport_send_tcp(uint32_t client_id, const uint8_t* data, uint16_t len)
{
    if (client_id >= MAX_TCP_CLIENTS) return -1;
    if (!tcp_clients[client_id].connected) return -1;

    struct tcp_pcb* pcb = tcp_clients[client_id].pcb;
    if (!pcb) return -1;

    err_t err = tcp_write(pcb, data, len, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) return -1;

    tcp_output(pcb);
    return len;
}
