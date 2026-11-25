// Host test: a pad config that uses I2C expander pins (100-115, 200-215) must
// survive a save and load. Saves the real Alpakka config through the real
// pad_config_to_flash() and loads it back through pad_config_load_runtime(),
// with the flash store replaced by a RAM copy.
//
// Build (from the repo root, one line):
//   gcc -std=c11 -Wall -Isrc -Isrc/pad
//   tests/pad_config_pins/test_pad_config_pins.c src/pad/pad_config_flash.c
//   -o test_pad_config_pins && ./test_pad_config_pins
// Pass criterion: prints "ALL PASS" and exits 0.

#include <stdio.h>
#include <string.h>
#include "pad_config_flash.h"
#include "pad_config_storage.h"
#include "configs/alpakka.h"

// RAM stand-in for the flash store.
static pad_config_flash_t stored;
static bool have_stored = false;
void pad_config_storage_init(void) {}
bool pad_config_storage_load(pad_config_flash_t* out) {
    if (!have_stored) return false;
    *out = stored;
    return true;
}
void pad_config_storage_save(const pad_config_flash_t* config) { stored = *config; have_stored = true; }
void pad_config_storage_erase(void) { have_stored = false; }

static int fails = 0;
#define CHECK(c, msg) do { if (c) printf("PASS %s\n", msg); else { printf("FAIL %s\n", msg); fails++; } } while (0)

int main(void)
{
    CHECK(pad_config_alpakka.dpad_up >= 100, "the Alpakka config really uses expander pins");

    pad_config_save(&pad_config_alpakka);
    const pad_device_config_t* back = pad_config_load_runtime();
    CHECK(back != NULL, "a saved expander config loads");
    CHECK(back && back->dpad_up == pad_config_alpakka.dpad_up, "expander pin 100+ comes back unchanged");
    CHECK(back && back->b1 == pad_config_alpakka.b1, "face button pin comes back unchanged");

    // Out-of-range pins must still be rejected.
    pad_config_flash_t bad;
    pad_config_to_flash(&pad_config_alpakka, &bad);
    bad.buttons[0] = 150;  // between the two expanders: not a real pin
    pad_config_storage_save(&bad);
    CHECK(pad_config_load_runtime() == NULL, "pin 150 is still rejected");
    bad.buttons[0] = 216;  // past the end of expander 1
    pad_config_storage_save(&bad);
    CHECK(pad_config_load_runtime() == NULL, "pin 216 is still rejected");

    printf(fails ? "SOME FAILED\n" : "ALL PASS\n");
    return fails ? 1 : 0;
}
