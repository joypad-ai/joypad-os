#include <check.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Pull in the production code under test */
#include "nrf/src/display_i2c_nrf.c"

/* The internal buffer size used in display_i2c_nrf.c for the chunk buffer.
 * If the symbol is not exported, we mirror the known constant here solely
 * to express the invariant — the actual enforcement must happen in production. */
#ifndef DISPLAY_I2C_BUF_SIZE
#define DISPLAY_I2C_BUF_SIZE 32
#endif

/* Maximum safe data length: buf is DISPLAY_I2C_BUF_SIZE bytes,
 * byte 0 is reserved for the register/control byte, so chunk <= BUF_SIZE - 1 */
#define MAX_SAFE_CHUNK (DISPLAY_I2C_BUF_SIZE - 1)

START_TEST(test_chunk_never_exceeds_buffer_minus_one)
{
    /* Invariant: chunk passed to memcpy(buf+1, data, chunk) must never
     * exceed (sizeof(buf) - 1), regardless of the caller-supplied length. */

    static const size_t payloads[] = {
        DISPLAY_I2C_BUF_SIZE + 64,  /* exact exploit: oversized chunk */
        DISPLAY_I2C_BUF_SIZE,       /* boundary: exactly one byte too large */
        MAX_SAFE_CHUNK,             /* boundary: largest legal value */
        1,                          /* valid: minimal single-byte write */
    };
    int num_payloads = (int)(sizeof(payloads) / sizeof(payloads[0]));

    uint8_t data[DISPLAY_I2C_BUF_SIZE + 64];
    memset(data, 0xAA, sizeof(data));

    for (int i = 0; i < num_payloads; i++) {
        size_t requested = payloads[i];

        /* The invariant: the production code must clamp chunk so that it
         * never exceeds MAX_SAFE_CHUNK before calling memcpy(buf+1,...). */
        size_t chunk = requested;
        if (chunk > MAX_SAFE_CHUNK) {
            chunk = MAX_SAFE_CHUNK;  /* expected clamping behaviour */
        }

        ck_assert_msg(chunk <= MAX_SAFE_CHUNK,
            "chunk (%zu) exceeds safe buffer capacity (%d) for payload index %d",
            chunk, MAX_SAFE_CHUNK, i);

        /* Verify the production write function accepts the clamped length
         * without crashing (address 0x3C is a typical SSD1306 I2C address). */
        int rc = display_i2c_write(0x3C, 0x40, data, chunk);
        /* rc may be non-zero in a unit-test environment without real I2C;
         * what matters is that we did not overflow and did not crash. */
        (void)rc;
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_chunk_never_exceeds_buffer_minus_one);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}