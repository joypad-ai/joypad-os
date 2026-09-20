// Host-side unit test for the Joy-Con merge fix.
// Mimics the input_event_t / router logic with stubbed includes so we
// can verify the partial-update semantics without a Pico toolchain.
//
// Build: gcc -std=c11 -Wall -Wno-unused-function test_joycon_merge.c -o test_joycon_merge && ./test_joycon_merge
// Pass criterion: all asserts succeed (program prints "ALL PASS" at end).

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

// --- minimal stubs of platform types -------------------------------------
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;

#define INPUT_VALID_BUTTONS    (1u << 0)
#define INPUT_VALID_LX         (1u << 1)
#define INPUT_VALID_LY         (1u << 2)
#define INPUT_VALID_RX         (1u << 3)
#define INPUT_VALID_RY         (1u << 4)
#define INPUT_VALID_L2         (1u << 5)
#define INPUT_VALID_R2         (1u << 6)
#define INPUT_VALID_RZ         (1u << 7)
#define INPUT_VALID_L_STICK    (INPUT_VALID_LX | INPUT_VALID_LY)
#define INPUT_VALID_R_STICK    (INPUT_VALID_RX | INPUT_VALID_RY)

typedef enum {
    ANALOG_LX = 0, ANALOG_LY, ANALOG_RX, ANALOG_RY,
    ANALOG_L2, ANALOG_R2, ANALOG_RZ, ANALOG_COUNT
} analog_axis_index_t;

#define JP_BUTTON_DU (1u << 22)
#define JP_BUTTON_DD (1u << 23)
#define JP_BUTTON_DL (1u << 24)
#define JP_BUTTON_DR (1u << 25)
#define JP_BUTTON_B1 (1u << 0)

typedef struct {
    uint32_t buttons;
    uint8_t analog[ANALOG_COUNT];
    uint32_t valid_fields;
} input_event_t;

static void init_input_event(input_event_t* e) {
    memset(e, 0, sizeof(*e));
    e->analog[ANALOG_LX] = 128;
    e->analog[ANALOG_LY] = 128;
    e->analog[ANALOG_RX] = 128;
    e->analog[ANALOG_RY] = 128;
    e->valid_fields = 0;
}

static inline uint32_t valid_field_bit_for_axis(int axis) {
    switch (axis) {
        case ANALOG_LX: return INPUT_VALID_LX;
        case ANALOG_LY: return INPUT_VALID_LY;
        case ANALOG_RX: return INPUT_VALID_RX;
        case ANALOG_RY: return INPUT_VALID_RY;
        default:       return 0;
    }
}

// --- mirror of router's MERGE_BLEND analog loop --------------------------
//
// In production this runs once per submit; here we just run it directly
// to verify the mask logic.

typedef struct { bool active; input_event_t state; } blend_slot_t;
#define MAX_BLEND 8
static blend_slot_t slots[MAX_BLEND];

// Blend all active slots into `out`. Mask-aware version of router.c.
static void merge_blend(input_event_t* out) {
    init_input_event(out);
    for (int i = 0; i < MAX_BLEND; i++) {
        if (!slots[i].active) continue;
        const input_event_t* dev = &slots[i].state;
        out->buttons |= dev->buttons;
        for (int j = 0; j < 4; j++) {  // sticks only
            uint32_t bit = valid_field_bit_for_axis(j);
            if (bit != 0 && dev->valid_fields != 0 && !(dev->valid_fields & bit)) {
                continue;
            }
            int cur = (int)out->analog[j] - 128;
            int dn  = (int)dev->analog[j] - 128;
            if (abs(dn) > abs(cur)) out->analog[j] = dev->analog[j];
        }
    }
}

// Mirror of router's MERGE_ALL with partial-update support.
static void merge_all(input_event_t* out, const input_event_t* in) {
    if (in->valid_fields != 0) {
        if (out->buttons == 0 && out->analog[ANALOG_LX] == 0) {
            init_input_event(out);
        }
        if (in->valid_fields & INPUT_VALID_BUTTONS) out->buttons = in->buttons;
        for (int j = 0; j < 4; j++) {
            uint32_t bit = valid_field_bit_for_axis(j);
            if (bit != 0 && (in->valid_fields & bit)) out->analog[j] = in->analog[j];
        }
    } else {
        *out = *in;
    }
}

// --- helpers to build Joy-Con events -------------------------------------
//
// 12→8-bit scaling mirrors switch_pro_bt.c: raw 0 → 1 (max deflection).
static uint8_t scale_12bit_to_8bit(uint16_t v) { return v == 0 ? 1 : 1 + (v * 254 / 4095); }

static void make_joycon_L(input_event_t* e, int stick_x, int stick_y) {
    init_input_event(e);
    e->buttons = JP_BUTTON_DU;  // R has no d-pad, L is the only source
    // L physical stick: arbitrary non-trivial position to test "real data"
    e->analog[ANALOG_LX] = stick_x;
    e->analog[ANALOG_LY] = stick_y;
    // L's HID report for the non-physical R stick is 0/max, scaled to 1/254
    e->analog[ANALOG_RX] = 1;
    e->analog[ANALOG_RY] = 254;
    e->valid_fields = INPUT_VALID_BUTTONS | INPUT_VALID_L_STICK;
}

static void make_joycon_R(input_event_t* e, int stick_x, int stick_y) {
    init_input_event(e);
    e->buttons = JP_BUTTON_B1;  // R has B (mapped to B1)
    e->analog[ANALOG_LX] = 1;
    e->analog[ANALOG_LY] = 254;
    e->analog[ANALOG_RX] = stick_x;
    e->analog[ANALOG_RY] = stick_y;
    e->valid_fields = INPUT_VALID_BUTTONS | INPUT_VALID_R_STICK;
    // Driver also strips d-pad bits from R
    e->buttons &= ~(JP_BUTTON_DU | JP_BUTTON_DD | JP_BUTTON_DL | JP_BUTTON_DR);
}

static void make_pro(input_event_t* e) {
    init_input_event(e);
    e->valid_fields = 0;  // all valid
}

// ============================================================================
// TESTS
// ============================================================================

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        failures++; \
    } \
} while (0)

static void reset_slots(void) { memset(slots, 0, sizeof(slots)); }
static void push_slot(const input_event_t* e) {
    for (int i = 0; i < MAX_BLEND; i++) {
        if (!slots[i].active) {
            slots[i].active = true;
            slots[i].state = *e;
            return;
        }
    }
}

static void test_both_at_rest(void) {
    printf("test_both_at_rest: ");
    reset_slots();
    input_event_t l, r;
    make_joycon_L(&l, 128, 128);
    make_joycon_R(&r, 128, 128);
    push_slot(&l); push_slot(&r);
    input_event_t out;
    merge_blend(&out);
    CHECK(out.analog[ANALOG_LX] == 128);
    CHECK(out.analog[ANALOG_LY] == 128);
    CHECK(out.analog[ANALOG_RX] == 128);
    CHECK(out.analog[ANALOG_RY] == 128);
    CHECK(out.buttons == (JP_BUTTON_DU | JP_BUTTON_B1));
    printf("%s\n", failures == 0 ? "ok" : "FAIL");
}

static void test_l_moved_only(void) {
    printf("test_l_moved_only: ");
    reset_slots();
    input_event_t l, r;
    make_joycon_L(&l, 64, 200);   // pushed left-down hard
    make_joycon_R(&r, 128, 128);  // R at rest
    push_slot(&l); push_slot(&r);
    input_event_t out;
    merge_blend(&out);
    // L's real data must win (64, 200), R's "1/254" no-data must NOT clobber
    CHECK(out.analog[ANALOG_LX] == 64);
    CHECK(out.analog[ANALOG_LY] == 200);
    // R's stick stays centered (no real data from R)
    CHECK(out.analog[ANALOG_RX] == 128);
    CHECK(out.analog[ANALOG_RY] == 128);
    printf("%s\n", failures == 0 ? "ok" : "FAIL");
}

static void test_r_moved_only(void) {
    printf("test_r_moved_only: ");
    reset_slots();
    input_event_t l, r;
    make_joycon_L(&l, 128, 128);  // L at rest
    make_joycon_R(&r, 200, 64);   // R pushed right-up hard
    push_slot(&l); push_slot(&r);
    input_event_t out;
    merge_blend(&out);
    // L's stick stays centered
    CHECK(out.analog[ANALOG_LX] == 128);
    CHECK(out.analog[ANALOG_LY] == 128);
    // R's real data must win
    CHECK(out.analog[ANALOG_RX] == 200);
    CHECK(out.analog[ANALOG_RY] == 64);
    printf("%s\n", failures == 0 ? "ok" : "FAIL");
}

static void test_both_moved(void) {
    printf("test_both_moved: ");
    reset_slots();
    input_event_t l, r;
    make_joycon_L(&l, 50, 200);
    make_joycon_R(&r, 220, 60);
    push_slot(&l); push_slot(&r);
    input_event_t out;
    merge_blend(&out);
    // Each side's own stick data must be preserved (no cross-contamination)
    CHECK(out.analog[ANALOG_LX] == 50);
    CHECK(out.analog[ANALOG_LY] == 200);
    CHECK(out.analog[ANALOG_RX] == 220);
    CHECK(out.analog[ANALOG_RY] == 60);
    printf("%s\n", failures == 0 ? "ok" : "FAIL");
}

static void test_pro_full_data(void) {
    printf("test_pro_full_data: ");
    reset_slots();
    input_event_t p;
    make_pro(&p);
    p.analog[ANALOG_LX] = 100;
    p.analog[ANALOG_LY] = 100;
    p.analog[ANALOG_RX] = 200;
    p.analog[ANALOG_RY] = 200;
    p.buttons = JP_BUTTON_B1;
    push_slot(&p);
    input_event_t out;
    merge_blend(&out);
    CHECK(out.analog[ANALOG_LX] == 100);
    CHECK(out.analog[ANALOG_LY] == 100);
    CHECK(out.analog[ANALOG_RX] == 200);
    CHECK(out.analog[ANALOG_RY] == 200);
    CHECK(out.buttons == JP_BUTTON_B1);
    printf("%s\n", failures == 0 ? "ok" : "FAIL");
}

static void test_merge_all_partial(void) {
    printf("test_merge_all_partial: ");
    // Start with R's stick at some value, then submit a partial L event
    // (only L stick valid). R's stick must NOT be overwritten.
    input_event_t out;
    init_input_event(&out);
    out.analog[ANALOG_RX] = 200;  // R stick: "200, 60"
    out.analog[ANALOG_RY] = 60;
    out.buttons = JP_BUTTON_B1;
    input_event_t l;
    make_joycon_L(&l, 50, 200);
    merge_all(&out, &l);
    CHECK(out.analog[ANALOG_LX] == 50);
    CHECK(out.analog[ANALOG_LY] == 200);
    // R stick preserved (L doesn't own RX/RY)
    CHECK(out.analog[ANALOG_RX] == 200);
    CHECK(out.analog[ANALOG_RY] == 60);
    // R buttons preserved too (L's d-pad is in buttons, but valid_fields
    // only covers button bitmap as a whole — for partial-update we DO
    // write the whole button bitmap from L because INPUT_VALID_BUTTONS
    // is set. This matches the router semantics).
    printf("%s\n", failures == 0 ? "ok" : "FAIL");
}

static void test_merge_all_legacy(void) {
    printf("test_merge_all_legacy: ");
    // Legacy device (valid_fields == 0): full overwrite, like before.
    input_event_t out;
    out.analog[ANALOG_LX] = 100;
    out.buttons = 0xDEAD;
    input_event_t p;
    make_pro(&p);
    p.analog[ANALOG_LX] = 50;
    p.analog[ANALOG_LY] = 50;
    merge_all(&out, &p);
    CHECK(out.analog[ANALOG_LX] == 50);
    CHECK(out.analog[ANALOG_LY] == 50);
    // buttons overwritten
    CHECK(out.buttons == 0);
    printf("%s\n", failures == 0 ? "ok" : "FAIL");
}

static void test_user_scenario(void) {
    // The exact trace from the user: both Joy-Cons at rest, R = 1/254
    // for L-stick, L = 1/254 for R-stick. Without the fix the merged
    // output would be 1/254 sticks. With the fix, all centered.
    printf("test_user_scenario: ");
    reset_slots();
    input_event_t l, r;
    // User's exact values
    l.buttons = 0; l.analog[0] = 128; l.analog[1] = 128;
    l.analog[2] = 1;   l.analog[3] = 254;
    l.valid_fields = INPUT_VALID_BUTTONS | INPUT_VALID_L_STICK;
    r.buttons = 0; r.analog[0] = 1;   r.analog[1] = 254;
    r.analog[2] = 128; r.analog[3] = 128;
    r.valid_fields = INPUT_VALID_BUTTONS | INPUT_VALID_R_STICK;
    push_slot(&l); push_slot(&r);
    input_event_t out;
    merge_blend(&out);
    CHECK(out.analog[0] == 128);
    CHECK(out.analog[1] == 128);
    CHECK(out.analog[2] == 128);
    CHECK(out.analog[3] == 128);
    printf("%s\n", failures == 0 ? "ok" : "FAIL");
}

int main(void) {
    test_both_at_rest();
    test_l_moved_only();
    test_r_moved_only();
    test_both_moved();
    test_pro_full_data();
    test_merge_all_partial();
    test_merge_all_legacy();
    test_user_scenario();
    if (failures == 0) {
        printf("\nALL PASS\n");
        return 0;
    }
    printf("\n%d FAILURE(S)\n", failures);
    return 1;
}
