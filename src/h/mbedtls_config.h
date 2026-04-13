#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

/* 平台与熵（嵌入式禁用平台熵，改用自定义硬件熵） */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C
/* 需在工程中实现:
   int mbedtls_hardware_poll(void *data, unsigned char *out, size_t len, size_t *olen); */

/* 椭圆曲线与大数（ECDSA/ECDH 前置） */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
/* 至少启用一条适用于 ECDSA 的曲线；仅开 CURVE25519 不够 */
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
/* 如需再加：#define MBEDTLS_ECP_DP_SECP384R1_ENABLED */

/* 对称密码与散列 */
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_MD_C
#define MBEDTLS_CIPHER_C

/* X.509/PEM/PK 解析（PK_PARSE 与 X509_USE 的前置） */
#define MBEDTLS_OID_C
#define MBEDTLS_ASN1PARSE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C

/* TLS 客户端（仅示例启用 TLS1.2） */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2

/* 资源约束可酌情调小 */
#define MBEDTLS_SSL_MAX_CONTENT_LEN 4096

// #include "mbedtls/check_config.h"
#endif
