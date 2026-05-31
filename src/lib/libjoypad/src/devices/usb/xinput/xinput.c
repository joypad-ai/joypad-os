// devices/usb/xinput/xinput.c — Microsoft XInput state → input_event_t.

#include <joypad/devices/microsoft/xinput.h>
#include <joypad/buttons.h>
#include <joypad/layouts.h>

#include <string.h>

// XInput thumb sticks are signed int16_t. Scale to 0..255 with center 128 and
// invert Y for HID convention. The original firmware code used 256-N for
// inversion, which produces 256 at the natural center (Y=0). We use the same
// formula for byte-equivalence; the off-by-one at center is a known property
// of the asymmetric 0..255 range with center 128.
static uint8_t byte_scale(int16_t v) {
    int32_t x = ((int32_t)v + 32768) / 257;   // map [-32768..32767] -> [0..255]
    if (x < 1)   x = 1;                       // legacy firmware kept 1 floor
    if (x > 255) x = 255;
    return (uint8_t)x;
}

void joypad_xinput_caps(joypad_xinput_type_t type, joypad_caps_t* out) {
    if (!out) return;
    joypad_caps_init(out);

    out->layout = LAYOUT_MODERN_4FACE;
    out->axes_mask = JOYPAD_AXIS_LX | JOYPAD_AXIS_LY |
                     JOYPAD_AXIS_RX | JOYPAD_AXIS_RY |
                     JOYPAD_AXIS_L2 | JOYPAD_AXIS_R2;
    out->button_count = 10;
    out->has_dpad = true;
    out->has_rumble = true;
    out->has_dual_rumble = true;

    switch (type) {
        case JOYPAD_XINPUT_TYPE_XBOX_ONE:
            // Xbox One / Series controllers add trigger rumble (impulse triggers).
            out->has_trigger_rumble = true;
            out->reports_battery_level = true;   // depends on wired/wireless; surface true
            break;
        case JOYPAD_XINPUT_TYPE_XBOX_360_WIRELESS:
            out->reports_battery_level = true;
            out->is_wireless = true;
            break;
        case JOYPAD_XINPUT_TYPE_XBOX_360_WIRED:
            // wired — battery N/A
            break;
        case JOYPAD_XINPUT_TYPE_XBOX_OG:
            // Original Xbox Duke / S-controller — per-button analog pressure
            out->has_pressure = true;
            out->button_count = 8;   // no Guide button on OG
            break;
        default:
            break;
    }
}

void joypad_xinput_gamepad_to_event(const joypad_xinput_gamepad_t* pad,
                                    joypad_xinput_type_t type,
                                    input_event_t* out) {
    if (!pad || !out) return;

    init_input_event(out);
    out->type = INPUT_TYPE_GAMEPAD;
    out->transport = INPUT_TRANSPORT_USB;
    out->layout = LAYOUT_MODERN_4FACE;
    out->button_count = (type == JOYPAD_XINPUT_TYPE_XBOX_OG) ? 8 : 10;

    out->buttons =
        ((pad->wButtons & JOYPAD_XINPUT_BTN_DPAD_UP)        ? JP_BUTTON_DU : 0u) |
        ((pad->wButtons & JOYPAD_XINPUT_BTN_DPAD_DOWN)      ? JP_BUTTON_DD : 0u) |
        ((pad->wButtons & JOYPAD_XINPUT_BTN_DPAD_LEFT)      ? JP_BUTTON_DL : 0u) |
        ((pad->wButtons & JOYPAD_XINPUT_BTN_DPAD_RIGHT)     ? JP_BUTTON_DR : 0u) |
        ((pad->wButtons & JOYPAD_XINPUT_BTN_A)              ? JP_BUTTON_B1 : 0u) |   // south
        ((pad->wButtons & JOYPAD_XINPUT_BTN_B)              ? JP_BUTTON_B2 : 0u) |   // east
        ((pad->wButtons & JOYPAD_XINPUT_BTN_X)              ? JP_BUTTON_B3 : 0u) |   // west
        ((pad->wButtons & JOYPAD_XINPUT_BTN_Y)              ? JP_BUTTON_B4 : 0u) |   // north
        ((pad->wButtons & JOYPAD_XINPUT_BTN_LEFT_SHOULDER)  ? JP_BUTTON_L1 : 0u) |
        ((pad->wButtons & JOYPAD_XINPUT_BTN_RIGHT_SHOULDER) ? JP_BUTTON_R1 : 0u) |
        // L2/R2 stay analog at the input_event_t level; digital edge synthesis
        // happens downstream when a consumer wants digital triggers.
        ((pad->wButtons & JOYPAD_XINPUT_BTN_BACK)           ? JP_BUTTON_S1 : 0u) |
        ((pad->wButtons & JOYPAD_XINPUT_BTN_START)          ? JP_BUTTON_S2 : 0u) |
        ((pad->wButtons & JOYPAD_XINPUT_BTN_LEFT_THUMB)     ? JP_BUTTON_L3 : 0u) |
        ((pad->wButtons & JOYPAD_XINPUT_BTN_RIGHT_THUMB)    ? JP_BUTTON_R3 : 0u) |
        ((pad->wButtons & JOYPAD_XINPUT_BTN_GUIDE)          ? JP_BUTTON_A1 : 0u) |
        ((pad->wButtons & JOYPAD_XINPUT_BTN_SHARE)          ? JP_BUTTON_A2 : 0u);

    out->analog[ANALOG_LX] = byte_scale(pad->sThumbLX);
    out->analog[ANALOG_LY] = (uint8_t)(256 - byte_scale(pad->sThumbLY));   // invert Y
    out->analog[ANALOG_RX] = byte_scale(pad->sThumbRX);
    out->analog[ANALOG_RY] = (uint8_t)(256 - byte_scale(pad->sThumbRY));
    out->analog[ANALOG_L2] = pad->bLeftTrigger;
    out->analog[ANALOG_R2] = pad->bRightTrigger;

    // Original Xbox Duke / S-controller per-button analog pressure. Canonical
    // pressure[] layout: {U,R,D,L, L2,R2,L1,R1, triangle,circle,cross,square}.
    if (type == JOYPAD_XINPUT_TYPE_XBOX_OG) {
        out->has_pressure = true;
        out->pressure[0]  = 0;
        out->pressure[1]  = 0;
        out->pressure[2]  = 0;
        out->pressure[3]  = 0;
        out->pressure[4]  = pad->bLeftTrigger;
        out->pressure[5]  = pad->bRightTrigger;
        out->pressure[6]  = pad->pressure_white;
        out->pressure[7]  = pad->pressure_black;
        out->pressure[8]  = pad->pressure_y;
        out->pressure[9]  = pad->pressure_b;
        out->pressure[10] = pad->pressure_a;
        out->pressure[11] = pad->pressure_x;
    }
}
