// ps4_mbedtls_config.h - Minimal mbedTLS configuration for PS4 RSA-PSS authentication
//
// Enables only the modules required for RSA-2048-PSS signing with SHA-256.
// This overrides the pico-sdk default mbedtls_config.h to avoid pulling in
// TLS, network, and other unneeded subsystems.
//
// Usage: compile with -DMBEDTLS_CONFIG_FILE="ps4_mbedtls_config.h"
// and ensure the containing directory is in the include path.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef PS4_MBEDTLS_CONFIG_H
#define PS4_MBEDTLS_CONFIG_H

// --- Core arithmetic ---
#define MBEDTLS_BIGNUM_C        // Multi-precision integer arithmetic (required by RSA)

// --- RSA ---
#define MBEDTLS_RSA_C           // RSA algorithm
#define MBEDTLS_PKCS1_V21       // PKCS#1 v2.1 (PSS padding mode)

// --- Hash ---
#define MBEDTLS_SHA256_C        // SHA-256

// --- Message digest abstraction ---
#define MBEDTLS_MD_C            // Generic message-digest wrapper

// --- OID (required by RSA internals) ---
#define MBEDTLS_OID_C

// --- Platform ---
#define MBEDTLS_PLATFORM_C           // Platform abstraction layer
#define MBEDTLS_NO_PLATFORM_ENTROPY  // No platform entropy source (we use pico_rand)

// Disable file I/O (not available on bare-metal)
#undef MBEDTLS_FS_IO
#undef MBEDTLS_NET_C
#undef MBEDTLS_TIMING_C

// Suppress date/time usage (not available)
#undef MBEDTLS_HAVE_TIME_DATE

// Reduce code size
#define MBEDTLS_AES_ROM_TABLES

#endif // PS4_MBEDTLS_CONFIG_H
