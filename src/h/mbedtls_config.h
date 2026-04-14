#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

/* 平台与熵 */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C
/* 需实现:
   int mbedtls_hardware_poll(void *data, unsigned char *out, size_t len, size_t *olen); */

/* 椭圆曲线与大数（ECDHE/ECDSA） */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
/* 可选：#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
         #define MBEDTLS_ECP_DP_CURVE25519_ENABLED  // 若需 X25519 */

/* 对称密码与散列 */
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_MD_C
#define MBEDTLS_CIPHER_C

/* X.509/PEM/PK 解析 */
#define MBEDTLS_OID_C
#define MBEDTLS_ASN1PARSE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C

/* TLS 协议 */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2

/* 资源可调 */
#define MBEDTLS_SSL_MAX_CONTENT_LEN 4096
// 只保留你真的要用的，比如 SHA256
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA256_ALT

// 关闭所有你没用到的高级功能
#undef MBEDTLS_ECDSA_C
#undef MBEDTLS_ENTROPY_C
#undef MBEDTLS_GCM_C
#undef MBEDTLS_PK_C
#undef MBEDTLS_PK_PARSE_C
#undef MBEDTLS_SSL_TLS_C
#undef MBEDTLS_X509_USE_C
#undef MBEDTLS_TLS_CLIENT
#undef MBEDTLS_TLS_SERVER

#include "mbedtls/check_config.h"
#endif
