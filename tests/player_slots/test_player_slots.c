// Host test: add_player() in FIXED slot mode must refuse a device when every
// slot is taken, instead of overwriting player 1.
//
// Build (from the repo root, one line):
//   gcc -std=c11 -Wall -Isrc -Itests/player_slots/stub -DCFG_TUD_CDC=0
//   tests/player_slots/test_player_slots.c src/core/services/players/manager.c
//   -o test_player_slots && ./test_player_slots
// Pass criterion: prints "ALL PASS" and exits 0.

#include <stdio.h>
#include <string.h>
#include "core/services/players/manager.h"

// Link stubs for the services manager.c calls; none affect slot logic.
void feedback_init(void) {}
#include <stdint.h>
void feedback_set_led_player(uint8_t player_index, uint8_t player_num) { (void)player_index; (void)player_num; }
void profile_indicator_init(void) {}
void profile_indicator_task(void) {}
void router_reset_outputs(void) {}

static int fails = 0;
#define CHECK(c, msg) do { if (c) printf("PASS %s\n", msg); else { printf("FAIL %s\n", msg); fails++; } } while (0)

int main(void)
{
    players_init();
    players_set_slot_mode(PLAYER_SLOT_FIXED);

    for (int i = 0; i < MAX_PLAYERS; i++) {
        int idx = add_player(10 + i, 0, INPUT_TRANSPORT_USB, "pad");
        CHECK(idx == i, "fills the next free slot");
    }

    int extra = add_player(99, 0, INPUT_TRANSPORT_USB, "extra");
    CHECK(extra == -1, "a device beyond the last slot is refused");
    CHECK(players[0].dev_addr == 10, "player 1 keeps its device");
    CHECK(find_player_index(99, 0) == -1, "the refused device has no slot");

    remove_players_by_address(12, -1);
    int again = add_player(77, 0, INPUT_TRANSPORT_USB, "late");
    CHECK(again == 2, "a freed slot is reused");

    printf(fails ? "SOME FAILED\n" : "ALL PASS\n");
    return fails ? 1 : 0;
}
