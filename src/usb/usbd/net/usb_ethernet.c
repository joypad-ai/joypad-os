/*
 * USB Ethernet (ECM/RNDIS) for Joypad-OS
 * Based on GP2040-CE's rndis.c (MIT License, Peter Lawrence)
 */
#include "tusb.h"

#if CFG_TUD_ECM_RNDIS || CFG_TUD_NCM

#include "usb_ethernet.h"
#include "dhserver.h"
#include "dnserver.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include <string.h>
#include <stdio.h>

#define INIT_IP4(a, b, c, d) { PP_HTONL(LWIP_MAKEU32(a, b, c, d)) }

static struct netif netif_data;
static struct pbuf *received_frame;
static bool net_initialized = false;

uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x6A, 0x96, 0x00};

static const ip4_addr_t ipaddr  = INIT_IP4(192, 168, 7, 1);
static const ip4_addr_t netmask = INIT_IP4(255, 255, 255, 0);
static const ip4_addr_t gateway = INIT_IP4(0, 0, 0, 0);

static dhcp_entry_t dhcp_entries[] = {
    { {0}, INIT_IP4(192, 168, 7, 2), 24 * 60 * 60 },
    { {0}, INIT_IP4(192, 168, 7, 3), 24 * 60 * 60 },
    { {0}, INIT_IP4(192, 168, 7, 4), 24 * 60 * 60 },
};

static const dhcp_config_t dhcp_config = {
    .router = INIT_IP4(0, 0, 0, 0),
    .port = 67,
    .dns = INIT_IP4(192, 168, 7, 1),
    "usb",
    TU_ARRAY_SIZE(dhcp_entries),
    dhcp_entries
};

static err_t linkoutput_fn(struct netif *netif, struct pbuf *p) {
    (void)netif;
    for (;;) {
        if (!tud_ready()) return ERR_USE;
        if (tud_network_can_xmit(p->tot_len)) {
            tud_network_xmit(p, 0);
            return ERR_OK;
        }
        tud_task();
    }
}

static err_t ip4_output_fn(struct netif *netif, struct pbuf *p, const ip4_addr_t *addr) {
    return etharp_output(netif, p, addr);
}

static err_t netif_init_cb(struct netif *netif) {
    LWIP_ASSERT("netif != NULL", (netif != NULL));
    netif->mtu = CFG_TUD_NET_MTU;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
    netif->state = NULL;
    netif->name[0] = 'E';
    netif->name[1] = 'X';
    netif->linkoutput = linkoutput_fn;
    netif->output = ip4_output_fn;
    return ERR_OK;
}

static void init_lwip(void) {
    struct netif *netif = &netif_data;
    lwip_init();
    netif->hwaddr_len = sizeof(tud_network_mac_address);
    memcpy(netif->hwaddr, tud_network_mac_address, sizeof(tud_network_mac_address));
    netif->hwaddr[5] ^= 0x01;
    netif = netif_add(netif, &ipaddr, &netmask, &gateway, NULL, netif_init_cb, ip_input);
    netif_set_default(netif);
}

static uint32_t recv_cb_count = 0;
bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
    recv_cb_count++;
    if (recv_cb_count <= 5 || recv_cb_count % 100 == 0) {
        printf("[net] recv_cb #%lu size=%u\n", (unsigned long)recv_cb_count, size);
    }
    if (received_frame) return false;
    if (size) {
        struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
        if (p) { memcpy(p->payload, src, size); received_frame = p; }
    }
    return true;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
    struct pbuf *p = (struct pbuf *)ref;
    (void)arg;
    return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

void tud_network_init_cb(void) {
    if (received_frame) { pbuf_free(received_frame); received_frame = NULL; }
}

bool dns_query_proc(const char *name, ip4_addr_t *addr) {
    if (0 == strcmp(name, "joypad.usb") || 0 == strcmp(name, "config.usb")) {
        *addr = ipaddr;
        return true;
    }
    return false;
}

static void service_traffic(void) {
    if (received_frame) {
        ethernet_input(received_frame, &netif_data);
        pbuf_free(received_frame);
        received_frame = NULL;
        tud_network_recv_renew();
    }
    sys_check_timeouts();
}

int usb_ethernet_init(void) {
    printf("[net] Initializing USB Ethernet...\n");
    init_lwip();
    printf("[net] lwIP initialized, netif up=%d\n", netif_is_up(&netif_data));

    err_t err = dhserv_init(&dhcp_config);
    printf("[net] DHCP server init: %d\n", err);

    err = dnserv_init(&ipaddr, 53, dns_query_proc);
    printf("[net] DNS server init: %d\n", err);

    net_initialized = true;
    printf("[net] USB Ethernet ready at 192.168.7.1\n");
    return 0;
}

static uint32_t init_delay = 0;

void usb_ethernet_task(void) {
    /* Lazy init: wait ~3 seconds after boot for USB to enumerate */
    if (!net_initialized) {
        init_delay++;
        if (init_delay >= 3000) {  /* ~3s at 1ms loop */
            printf("[net] Deferred init — starting lwIP + DHCP\n");
            usb_ethernet_init();
        }
        return;
    }
    service_traffic();
}

bool usb_ethernet_is_active(void) {
    return net_initialized && tud_ready();
}

sys_prot_t sys_arch_protect(void) { return 0; }
void sys_arch_unprotect(sys_prot_t pval) { (void)pval; }

#if defined(__ZEPHYR__)
#include <zephyr/kernel.h>
uint32_t sys_now(void) { return k_uptime_get_32(); }
#elif defined(PICO_BOARD)
#include "pico/time.h"
uint32_t sys_now(void) { return to_ms_since_boot(get_absolute_time()); }
#else
#include <esp_timer.h>
uint32_t sys_now(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
#endif

#endif /* CFG_TUD_ECM_RNDIS || CFG_TUD_NCM */
