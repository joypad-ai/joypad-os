#ifndef _LWIPOPTS_H_
#define _LWIPOPTS_H_

#define NO_SYS                  1
#define MEM_ALIGNMENT           4
#define MEM_SIZE                (8 * 1024)
#define MEMP_NUM_PBUF           16
#define MEMP_NUM_UDP_PCB        4
#define MEMP_NUM_TCP_PCB        4
#define MEMP_NUM_TCP_SEG        16
#define MEMP_NUM_ARP_QUEUE      8
#define PBUF_POOL_SIZE          16
#define PBUF_POOL_BUFSIZE       512
#define LWIP_ARP                1
#define LWIP_ETHERNET           1
#define LWIP_RAW                0
#define LWIP_NETCONN            0
#define LWIP_SOCKET             0
#define LWIP_STATS              0
#define LWIP_IPV4               1
#define LWIP_IPV6               0
#define LWIP_ICMP               1
#define LWIP_IGMP               0
#define LWIP_DHCP               0
#define LWIP_UDP                1
#define LWIP_TCP                1
#define TCP_MSS                 (1500 - 40)
#define TCP_WND                 (4 * TCP_MSS)
#define TCP_SND_BUF             (4 * TCP_MSS)
#define IP_FRAG                 0
#define IP_REASSEMBLY           0
#define LWIP_HTTPD              0
#define CHECKSUM_GEN_IP         1
#define CHECKSUM_GEN_UDP        1
#define CHECKSUM_GEN_TCP        1
#define CHECKSUM_CHECK_IP       1
#define CHECKSUM_CHECK_UDP      1
#define CHECKSUM_CHECK_TCP      1
#define LWIP_DEBUG              0

#endif
