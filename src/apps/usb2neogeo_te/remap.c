#include "remap.h"
#include "core/buttons.h"
#include "platform/platform.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Context init — always resets to default mapping.
// Called whenever a controller connects; no flash involved.
// ---------------------------------------------------------------------------

void neogeo_remap_ctx_init(neogeo_remap_ctx_t *ctx) {
    memset(ctx, 0, sizeof(neogeo_remap_ctx_t));
    ctx->state           = REMAP_STATE_IDLE;
    ctx->boot_checked    = false;
    ctx->mapped_mask     = 0;
    ctx->error_flash_ms  = 0;
    ctx->rumble_start_ms = 0;
    ctx->completed       = false;
    ctx->connect_ms      = platform_time_ms();
    ctx->active_trigger  = 0;
}

// ---------------------------------------------------------------------------
// Per-frame update
//
// Call this from the app's translate/task function before applying the remap
// table. Returns true while remap mode is active — caller should suppress
// normal Neo Geo output during this time.
//
// The frame where state transitions into REMAP_STATE_WAITING is the signal
// to fire rumble/LED feedback to notify the user remap mode is ready.
// ---------------------------------------------------------------------------

bool neogeo_remap_update(neogeo_remap_ctx_t *ctx,
                         uint32_t current_buttons,
                         neogeo_remap_t *remap_out)
{
    uint32_t now = platform_time_ms();

    switch (ctx->state) {

    case REMAP_STATE_IDLE:
        // Once the boot window has closed, never check again this session
        if (ctx->boot_checked) return false;

        // Check all three trigger options -- main combo, Select alone, Start alone
        uint32_t trigger = 0;
        if ((current_buttons & NEOGEO_REMAP_TRIGGER_MASK) == NEOGEO_REMAP_TRIGGER_MASK) {
            trigger = NEOGEO_REMAP_TRIGGER_MASK;
        } else if ((current_buttons & NEOGEO_REMAP_TRIGGER_SELECT) &&
                   !(current_buttons & ~NEOGEO_REMAP_TRIGGER_SELECT)) {
            // Select alone -- no other buttons held
            trigger = NEOGEO_REMAP_TRIGGER_SELECT;
        } else if ((current_buttons & NEOGEO_REMAP_TRIGGER_START) &&
                   !(current_buttons & ~NEOGEO_REMAP_TRIGGER_START)) {
            // Start alone -- no other buttons held
            trigger = NEOGEO_REMAP_TRIGGER_START;
        }

        if (trigger) {
            ctx->state = REMAP_STATE_HOLDING;
            ctx->last_activity_ms = now;
            ctx->active_trigger = trigger;
        } else if ((now - ctx->connect_ms) > NEOGEO_REMAP_BOOT_WINDOW_MS) {
            // Boot window expired since controller connected
            ctx->boot_checked = true;
        }
        return false;

    case REMAP_STATE_HOLDING:
        if ((current_buttons & ctx->active_trigger) != ctx->active_trigger) {
            // Released early — close the boot window, go to normal play
            ctx->state = REMAP_STATE_IDLE;
            ctx->boot_checked = true;
            ctx->active_trigger = 0;
            return false;
        }
        if ((now - ctx->last_activity_ms) >= NEOGEO_REMAP_HOLD_MS) {
            // Hold threshold reached — activate remap mode
            ctx->state = REMAP_STATE_WAITING;
            ctx->last_activity_ms = now;
            ctx->count = 0;
            ctx->mapped_mask = 0;
            memset(&ctx->pending, 0, sizeof(neogeo_remap_t));
            return true; // caller fires rumble/LED feedback this frame
        }
        return false;

    case REMAP_STATE_WAITING:
        if ((now - ctx->last_activity_ms) > NEOGEO_REMAP_TIMEOUT_MS) {
            ctx->state = REMAP_STATE_IDLE;
            ctx->boot_checked = true;
            return false;
        }
        // Wait for all trigger buttons to be released before collecting,
        // so they don't accidentally register as the first mapped button.
        if ((current_buttons & ctx->active_trigger) == 0) {
            ctx->state = REMAP_STATE_COLLECTING;
            ctx->last_buttons = 0;
            ctx->last_activity_ms = now;
            ctx->mapped_mask = 0;
        }
        return true;

    case REMAP_STATE_COLLECTING: {
        if ((now - ctx->last_activity_ms) > NEOGEO_REMAP_TIMEOUT_MS) {
            ctx->state = REMAP_STATE_IDLE;
            ctx->boot_checked = true;
            return false;
        }

        uint32_t newly_pressed = current_buttons & ~ctx->last_buttons;
        ctx->last_buttons = current_buttons;

        if (newly_pressed == 0) return true;

        // Require exactly one button at a time
        if (newly_pressed & (newly_pressed - 1)) return true;

        // Ignore d-pad — not remappable
        uint32_t ignore_mask = JP_BUTTON_DU | JP_BUTTON_DD |
                               JP_BUTTON_DL | JP_BUTTON_DR;
        if (newly_pressed & ignore_mask) return true;

        // Ignore Start and Coin — always passthrough, never remappable
        if (newly_pressed & (JP_BUTTON_S1 | JP_BUTTON_S2)) return true;

        // Ignore buttons already assigned to another slot
        if (newly_pressed & ctx->mapped_mask) {
            printf("[remap] Button already mapped, ignoring\n");
            ctx->error_flash_ms = now;
            ctx->last_activity_ms = now;  // reset timeout so player has time to try again
            return true;
        }

        ctx->pending.buttons[ctx->count] = newly_pressed;
        ctx->mapped_mask |= newly_pressed;
        ctx->count++;
        ctx->last_activity_ms = now;

        if (ctx->count >= NEOGEO_REMAP_BUTTON_COUNT) {
            // All 6 slots filled — apply to active remap (RAM only, no flash)
            memcpy(remap_out, &ctx->pending, sizeof(neogeo_remap_t));
            ctx->completed = true;
            ctx->state = REMAP_STATE_DONE;
        }
        return true;
    }

    case REMAP_STATE_DONE:
        // One frame of DONE — caller can flash LED to confirm success
        ctx->state = REMAP_STATE_IDLE;
        ctx->boot_checked = true;
        return false;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Apply the remap table to a normalized input button bitmap.
//
// Returns a 6-bit mask:
//   bit 0 = A (B1/P1)
//   bit 1 = B (B2/P2)
//   bit 2 = C (B3/P3)
//   bit 3 = D (B4/K1)
//   bit 4 = Select (B5/K2)
//   bit 5 = K3 (B6)
//
// Start, Coin, and D-Pad are handled separately by the output layer.
// ---------------------------------------------------------------------------

uint8_t neogeo_remap_apply(const neogeo_remap_t *remap, uint32_t buttons) {
    uint8_t out = 0;
    for (uint8_t i = 0; i < NEOGEO_REMAP_BUTTON_COUNT; i++) {
        if (buttons & remap->buttons[i]) {
            out |= (1 << i);
        }
    }
    return out;
}