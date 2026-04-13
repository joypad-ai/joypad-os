/* mbedtls_config.h — TLS client for RP2040 (Pico W + lwIP sockets) */
#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

/* 基础平台封装 */
#define MBEDTLS_PLATFORM_C

/* RNG/熵：禁用平台熵源，改由硬件/自定义提供 */
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C
/* 你需要在工程中实现：
   int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen);
   从可靠硬件熵源填充随机数据（例如 Pico W 的CYW43 RNG或外设）。 */

/* 常用密码学模块（TLS 所需的精简集合） */
#define MBEDTLS_SHA256_C
#define MBEDTLS_AES_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_MD_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C

/* X.509 & PEM 解析（用于加载根证书等） */
#define MBEDTLS_OID_C
#define MBEDTLS_ASN1PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C

/* TLS 协议栈（仅客户端，TLS 1.2） */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2

/* 可选：若你的环境提供POSIX socket且需要使用 Mbed TLS 自带的 net_sockets 适配层，则启用：
   #define MBEDTLS_NET_C
   注意：需要可用的 <sys/socket.h> 等头文件（e.g., 启用 lwIP socket）。 */

/* 内存占用优化 */
#define MBEDTLS_SSL_MAX_CONTENT_LEN 4096
#define MBEDTLS_SSL_KEEP_PEER_CERTIFICATE  /* 若内存更紧，可移除并在会话建立后复制所需字段 */

// #include "mbedtls/check_config.h"
#endif /* MBEDTLS_CONFIG_H */
