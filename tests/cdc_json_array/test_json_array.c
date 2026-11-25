// Host-side unit test for the JSON integer-array parsers in
// src/usb/usbd/cdc/cdc_commands.c (json_get_int_array / json_get_int16_array).
// The parser below is a copy of the firmware loop so it can be run without a
// Pico toolchain. Keep it in sync with the source.
//
// Build: gcc -std=gnu11 -Wall test_json_array.c -o test_json_array && ./test_json_array
// Pass criterion: prints "ALL PASS". A parser that spins on a malformed array
// is killed by the 2 second alarm, which fails the run.

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

static int json_get_int_array(const char* json, const char* key,
                              uint8_t* out, int max_count)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":[", key);

    const char* start = strstr(json, search);
    if (!start) return 0;

    start += strlen(search);
    int count = 0;

    while (*start && count < max_count) {
        const char* before = start;

        // Skip whitespace
        while (*start == ' ' || *start == '\t') start++;

        if (*start == ']') break;

        // Parse number
        if (*start == '-' || (*start >= '0' && *start <= '9')) {
            out[count++] = (uint8_t)atoi(start);
            // Skip past number
            while (*start == '-' || (*start >= '0' && *start <= '9')) start++;
        }

        // Skip comma
        while (*start == ' ' || *start == '\t') start++;
        if (*start == ',') start++;

        // Anything else (a letter, a quote, a newline) is not consumed by
        // any branch above: stop here instead of spinning on it forever.
        if (start == before) break;
    }

    return count;
}

static void expect(const char* json, int want_count, const uint8_t* want)
{
    uint8_t out[8];
    memset(out, 0xEE, sizeof(out));
    int n = json_get_int_array(json, "k", out, 8);
    if (n != want_count || memcmp(out, want, (size_t)want_count) != 0) {
        printf("FAIL: %s -> count %d (want %d)\n", json, n, want_count);
        exit(1);
    }
}

int main(void)
{
    alarm(2);  // a hang is a failure, not a stuck test run

    const uint8_t v123[] = {1, 2, 3};
    const uint8_t v12[]  = {1, 2};
    const uint8_t v1[]   = {1};

    // Well-formed input: unchanged behaviour.
    expect("{\"k\":[1,2,3]}", 3, v123);
    expect("{\"k\":[ 1 , 2 ,\t3 ]}", 3, v123);
    expect("{\"k\":[]}", 0, v123);
    expect("{\"k\":[1,,2]}", 2, v12);
    expect("{\"k\":[1,2", 2, v12);          // truncated message
    expect("{\"other\":[1]}", 0, v123);     // key absent

    // Malformed input: return what was parsed so far, never spin.
    expect("{\"k\":[1,x,3]}", 1, v1);
    expect("{\"k\":[\"1\"]}", 0, v1);
    expect("{\"k\":[1.5]}", 1, v1);
    expect("{\"k\":[1\n,2]}", 1, v1);
    expect("{\"k\":[null]}", 0, v1);

    printf("ALL PASS\n");
    return 0;
}
