#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

/* 基础与平台 */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_NO_PLATFORM_ENTROPY  /* 嵌入式常用：禁用平台熵 */

/* 随机数与熵（用于 RSA-PSS 签名的随机盐；若你提供自有 RNG，可不启用这两项） */
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C
/* 若启用上面两项，需要实现：
   int mbedtls_hardware_poll(void *data, unsigned char *out, size_t len, size_t *olen);
   以提供硬件熵源（生产环境必备）。 */

/* 大数与RSA */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V21    /* 启用 RSA-PSS 支持 */

/* 消息摘要（PSS常用SHA-256） */
#define MBEDTLS_MD_C
#define MBEDTLS_SHA256_C

/* PK 接口与密钥解析（含PEM） */
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_ASN1PARSE_C
#define MBEDTLS_OID_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_BASE64_C

#include "mbedtls/check_config.h"
#endif
