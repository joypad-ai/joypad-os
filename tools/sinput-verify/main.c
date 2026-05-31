/*
 * sinput-verify: SDL3-based smoke test for the Joypad OS SInput gamepad firmware.
 *
 * Verifies that SDL3 enumerates the device (VID 0x2E8A / PID 0x10C6), exposes
 * the expected button/axis/sensor surface, and that rumble + LED + player-index
 * output reports round-trip back to the firmware.
 *
 * Build:  make
 * Run:    ./sinput-verify
 */

#include <SDL3/SDL.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define EXPECT_VID 0x2E8A
#define EXPECT_PID 0x10C6

#define ANSI_RESET   "\x1b[0m"
#define ANSI_BOLD    "\x1b[1m"
#define ANSI_DIM     "\x1b[2m"
#define ANSI_GREEN   "\x1b[32m"
#define ANSI_RED     "\x1b[31m"
#define ANSI_YELLOW  "\x1b[33m"
#define ANSI_CYAN    "\x1b[36m"

static volatile sig_atomic_t g_quit = 0;

static void on_sigint(int sig) {
    (void)sig;
    g_quit = 1;
}

static void pass_fail(const char *label, bool ok, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stdout, "  [%s%s%s] %-28s ",
            ok ? ANSI_GREEN : ANSI_RED,
            ok ? "PASS" : "FAIL",
            ANSI_RESET,
            label);
    vfprintf(stdout, fmt, ap);
    fputc('\n', stdout);
    va_end(ap);
}

static const char *joystick_type_str(SDL_JoystickType t) {
    switch (t) {
        case SDL_JOYSTICK_TYPE_UNKNOWN:       return "UNKNOWN";
        case SDL_JOYSTICK_TYPE_GAMEPAD:       return "GAMEPAD";
        case SDL_JOYSTICK_TYPE_WHEEL:         return "WHEEL";
        case SDL_JOYSTICK_TYPE_ARCADE_STICK:  return "ARCADE_STICK";
        case SDL_JOYSTICK_TYPE_FLIGHT_STICK:  return "FLIGHT_STICK";
        case SDL_JOYSTICK_TYPE_DANCE_PAD:     return "DANCE_PAD";
        case SDL_JOYSTICK_TYPE_GUITAR:        return "GUITAR";
        case SDL_JOYSTICK_TYPE_DRUM_KIT:      return "DRUM_KIT";
        case SDL_JOYSTICK_TYPE_ARCADE_PAD:    return "ARCADE_PAD";
        case SDL_JOYSTICK_TYPE_THROTTLE:      return "THROTTLE";
        default:                              return "?";
    }
}

static const char *gamepad_type_str(SDL_GamepadType t) {
    switch (t) {
        case SDL_GAMEPAD_TYPE_UNKNOWN:                       return "UNKNOWN";
        case SDL_GAMEPAD_TYPE_STANDARD:                      return "STANDARD";
        case SDL_GAMEPAD_TYPE_XBOX360:                       return "XBOX360";
        case SDL_GAMEPAD_TYPE_XBOXONE:                       return "XBOXONE";
        case SDL_GAMEPAD_TYPE_PS3:                           return "PS3";
        case SDL_GAMEPAD_TYPE_PS4:                           return "PS4";
        case SDL_GAMEPAD_TYPE_PS5:                           return "PS5";
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:           return "SWITCH_PRO";
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:   return "JOYCON_L";
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:  return "JOYCON_R";
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:   return "JOYCON_PAIR";
        default:                                             return "?";
    }
}

static void describe_joystick(SDL_JoystickID id) {
    const char *name   = SDL_GetJoystickNameForID(id);
    const char *path   = SDL_GetJoystickPathForID(id);
    uint16_t    vid    = SDL_GetJoystickVendorForID(id);
    uint16_t    pid    = SDL_GetJoystickProductForID(id);
    SDL_JoystickType jt = SDL_GetJoystickTypeForID(id);
    SDL_GUID    guid   = SDL_GetJoystickGUIDForID(id);
    char        guid_s[33] = {0};
    SDL_GUIDToString(guid, guid_s, sizeof(guid_s));

    bool        is_pad = SDL_IsGamepad(id);
    SDL_GamepadType gt = is_pad ? SDL_GetGamepadTypeForID(id) : SDL_GAMEPAD_TYPE_UNKNOWN;
    const char *padname = is_pad ? SDL_GetGamepadNameForID(id) : NULL;
    const char *mapping = is_pad ? SDL_GetGamepadMappingForID(id) : NULL;

    printf(ANSI_BOLD "  joystick id=%u" ANSI_RESET "\n", (unsigned)id);
    printf("    name       : %s\n",        name ? name : "(null)");
    printf("    path       : %s\n",        path ? path : "(null)");
    printf("    vid:pid    : %04X:%04X\n", vid, pid);
    printf("    guid       : %s\n",        guid_s);
    printf("    joy type   : %s (%d)\n",   joystick_type_str(jt), (int)jt);
    printf("    is gamepad : %s\n",        is_pad ? "yes" : "no");
    if (is_pad) {
        printf("    pad name   : %s\n",                  padname ? padname : "(null)");
        printf("    pad type   : %s (%d)\n",             gamepad_type_str(gt), (int)gt);
        printf("    mapping    : %s\n",                  mapping ? mapping : "(null)");
        if (mapping) {
            SDL_free((void *)mapping);
        }
    }
}

static void log_caps(SDL_Gamepad *pad, SDL_Joystick *joy) {
    printf("\n" ANSI_BOLD "verifying SInput surface" ANSI_RESET "\n");

    int nbuttons = SDL_GetNumJoystickButtons(joy);
    int naxes    = SDL_GetNumJoystickAxes(joy);
    int nhats    = SDL_GetNumJoystickHats(joy);

    pass_fail("buttons >= 32",       nbuttons >= 32, "got %d", nbuttons);
    pass_fail("axes >= 6 (LX/LY/RX/RY/LT/RT)", naxes >= 6, "got %d axes, %d hats", naxes, nhats);

    SDL_GamepadType pt = SDL_GetGamepadType(pad);
    bool sinput_like = (pt == SDL_GAMEPAD_TYPE_STANDARD || pt == SDL_GAMEPAD_TYPE_UNKNOWN);
    pass_fail("gamepad type",         sinput_like, "%s (%d) — STANDARD/UNKNOWN both OK", gamepad_type_str(pt), (int)pt);

    bool has_accel = SDL_GamepadHasSensor(pad, SDL_SENSOR_ACCEL);
    bool has_gyro  = SDL_GamepadHasSensor(pad, SDL_SENSOR_GYRO);
    pass_fail("accel sensor",         has_accel, "%s", has_accel ? "present" : "absent (SDL may not expose it for STANDARD)");
    pass_fail("gyro sensor",          has_gyro,  "%s", has_gyro  ? "present" : "absent");

    if (has_accel) {
        if (!SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_ACCEL, true)) {
            fprintf(stderr, "    " ANSI_YELLOW "warn:" ANSI_RESET " enable accel: %s\n", SDL_GetError());
        }
    }
    if (has_gyro) {
        if (!SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_GYRO, true)) {
            fprintf(stderr, "    " ANSI_YELLOW "warn:" ANSI_RESET " enable gyro: %s\n", SDL_GetError());
        }
    }

    int ntouch = SDL_GetNumGamepadTouchpads(pad);
    pass_fail("touchpad count",       ntouch >= 0, "%d touchpad(s)", ntouch);

    bool has_rumble  = SDL_GetBooleanProperty(SDL_GetGamepadProperties(pad), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
    bool has_rgb     = SDL_GetBooleanProperty(SDL_GetGamepadProperties(pad), SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN, false);
    bool has_pllight = SDL_GetBooleanProperty(SDL_GetGamepadProperties(pad), SDL_PROP_GAMEPAD_CAP_PLAYER_LED_BOOLEAN, false);
    pass_fail("rumble capability",    has_rumble,  "%s", has_rumble  ? "yes" : "no");
    pass_fail("rgb led capability",   has_rgb,     "%s", has_rgb     ? "yes" : "no");
    pass_fail("player led capability",has_pllight, "%s", has_pllight ? "yes" : "no");

    const char *serial = SDL_GetGamepadSerial(pad);
    pass_fail("serial readable",      serial != NULL && serial[0] != '\0', "%s", serial ? serial : "(null)");
}

static int set_stdin_nonblock(void) {
    int fl = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (fl < 0) return -1;
    return fcntl(STDIN_FILENO, F_SETFL, fl | O_NONBLOCK);
}

static const char *axis_name(SDL_GamepadAxis a) {
    switch (a) {
        case SDL_GAMEPAD_AXIS_LEFTX:        return "LX";
        case SDL_GAMEPAD_AXIS_LEFTY:        return "LY";
        case SDL_GAMEPAD_AXIS_RIGHTX:       return "RX";
        case SDL_GAMEPAD_AXIS_RIGHTY:       return "RY";
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return "LT";
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:return "RT";
        default:                            return "?";
    }
}

static void help(void) {
    printf("\n" ANSI_BOLD "interactive keys (stdin)" ANSI_RESET "\n"
           "  r  rumble 500ms @ full\n"
           "  l  set RGB LED red\n"
           "  L  set RGB LED green\n"
           "  b  set RGB LED blue\n"
           "  o  set RGB LED off\n"
           "  p  cycle player index 0..3\n"
           "  ?  print this help\n"
           "  q  quit\n\n");
}

int main(void) {
    signal(SIGINT,  on_sigint);
    signal(SIGTERM, on_sigint);

    /* Line-buffer stdout so output stays in order when piped to a file/pager. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    printf(ANSI_BOLD "sinput-verify" ANSI_RESET " — SDL3 %d.%d.%d\n",
           SDL_VERSIONNUM_MAJOR(SDL_GetVersion()),
           SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
           SDL_VERSIONNUM_MICRO(SDL_GetVersion()));

    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    // Force HIDAPI + the SInput driver on explicitly, and crank joystick
    // logging so we can see whether HIDAPI even considers our device.
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_SINPUT", "1");
    SDL_SetLogPriority(SDL_LOG_CATEGORY_INPUT, SDL_LOG_PRIORITY_VERBOSE);

    if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK)) {
        fprintf(stderr, ANSI_RED "SDL_Init failed:" ANSI_RESET " %s\n", SDL_GetError());
        return 1;
    }

    // HIDAPI claims devices asynchronously on a background thread — it takes
    // over from the macOS IOKit driver a moment after init. Pump events for a
    // few seconds so the SInput HIDAPI driver has time to re-enumerate our
    // device as a proper gamepad before we snapshot the joystick list.
    printf("\n" ANSI_DIM "waiting for HIDAPI to claim devices..." ANSI_RESET "\n");
    for (int i = 0; i < 60; ++i) {
        SDL_PumpEvents();
        SDL_Event e;
        while (SDL_PollEvent(&e)) { /* drain */ }
        SDL_Delay(50);
    }

    int count = 0;
    SDL_JoystickID *ids = SDL_GetJoysticks(&count);
    if (!ids) {
        fprintf(stderr, ANSI_RED "SDL_GetJoysticks failed:" ANSI_RESET " %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    printf("\n" ANSI_BOLD "%d joystick(s) enumerated by SDL3" ANSI_RESET "\n", count);

    SDL_JoystickID target = 0;
    for (int i = 0; i < count; ++i) {
        describe_joystick(ids[i]);
        if (SDL_GetJoystickVendorForID(ids[i])  == EXPECT_VID &&
            SDL_GetJoystickProductForID(ids[i]) == EXPECT_PID) {
            target = ids[i];
        }
    }
    SDL_free(ids);

    if (target == 0) {
        fprintf(stderr, "\n" ANSI_RED "no device matching %04X:%04X found." ANSI_RESET
                        " plug in the firmware and re-run.\n", EXPECT_VID, EXPECT_PID);
        SDL_Quit();
        return 2;
    }

    printf("\n" ANSI_CYAN "opening %04X:%04X (joystick id %u)" ANSI_RESET "\n",
           EXPECT_VID, EXPECT_PID, (unsigned)target);

    SDL_Joystick *joy = SDL_OpenJoystick(target);
    if (!joy) {
        fprintf(stderr, ANSI_RED "SDL_OpenJoystick failed:" ANSI_RESET " %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Gamepad *pad = NULL;
    if (SDL_IsGamepad(target)) {
        pad = SDL_OpenGamepad(target);
        if (!pad) {
            fprintf(stderr, ANSI_YELLOW "warn: SDL_OpenGamepad failed:" ANSI_RESET " %s\n", SDL_GetError());
        }
    } else {
        fprintf(stderr, ANSI_YELLOW "warn:" ANSI_RESET
                " device is not recognized as a gamepad by SDL3 — "
                "joystick-only mode (SInput HIDAPI driver may be missing in this SDL3 build)\n");
    }

    if (pad) {
        log_caps(pad, joy);
    }

    help();

    if (set_stdin_nonblock() < 0) {
        fprintf(stderr, ANSI_YELLOW "warn:" ANSI_RESET " fcntl O_NONBLOCK: %s\n", strerror(errno));
    }

    int player_idx = 0;
    SDL_Event ev;

    while (!g_quit) {
        while (SDL_PollEvent(&ev)) {
            uint64_t ts = ev.common.timestamp;
            switch (ev.type) {
                case SDL_EVENT_QUIT:
                    g_quit = 1;
                    break;
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                case SDL_EVENT_GAMEPAD_BUTTON_UP:
                    printf("[%" PRIu64 "ns] btn %s  state=%d\n",
                           ts,
                           SDL_GetGamepadStringForButton((SDL_GamepadButton)ev.gbutton.button),
                           ev.gbutton.down);
                    break;
                case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                    printf("[%" PRIu64 "ns] axis %s  value=%6d\n",
                           ts,
                           axis_name((SDL_GamepadAxis)ev.gaxis.axis),
                           ev.gaxis.value);
                    break;
                case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
                case SDL_EVENT_JOYSTICK_BUTTON_UP:
                    if (!pad) {
                        printf("[%" PRIu64 "ns] joy btn %d  state=%d\n",
                               ts, ev.jbutton.button, ev.jbutton.down);
                    }
                    break;
                case SDL_EVENT_JOYSTICK_AXIS_MOTION:
                    if (!pad) {
                        printf("[%" PRIu64 "ns] joy axis %d  value=%6d\n",
                               ts, ev.jaxis.axis, ev.jaxis.value);
                    }
                    break;
                case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
                    printf("[%" PRIu64 "ns] sensor %d  [%.3f %.3f %.3f]\n",
                           ts, ev.gsensor.sensor,
                           ev.gsensor.data[0], ev.gsensor.data[1], ev.gsensor.data[2]);
                    break;
                case SDL_EVENT_JOYSTICK_REMOVED:
                case SDL_EVENT_GAMEPAD_REMOVED:
                    if (ev.jdevice.which == target) {
                        printf(ANSI_YELLOW "device removed.\n" ANSI_RESET);
                        g_quit = 1;
                    }
                    break;
                default:
                    break;
            }
        }

        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n == 1) {
            switch (c) {
                case 'q': case 'Q':
                    g_quit = 1;
                    break;
                case 'r':
                    if (pad) {
                        bool ok = SDL_RumbleGamepad(pad, 0xFFFF, 0xFFFF, 500);
                        printf("rumble: %s%s%s\n",
                               ok ? ANSI_GREEN : ANSI_RED,
                               ok ? "OK" : SDL_GetError(),
                               ANSI_RESET);
                    }
                    break;
                case 'l':
                    if (pad) {
                        bool ok = SDL_SetGamepadLED(pad, 255, 0, 0);
                        printf("led red: %s\n", ok ? ANSI_GREEN "OK" ANSI_RESET : SDL_GetError());
                    }
                    break;
                case 'L':
                    if (pad) {
                        bool ok = SDL_SetGamepadLED(pad, 0, 255, 0);
                        printf("led green: %s\n", ok ? ANSI_GREEN "OK" ANSI_RESET : SDL_GetError());
                    }
                    break;
                case 'b':
                    if (pad) {
                        bool ok = SDL_SetGamepadLED(pad, 0, 0, 255);
                        printf("led blue: %s\n", ok ? ANSI_GREEN "OK" ANSI_RESET : SDL_GetError());
                    }
                    break;
                case 'o':
                    if (pad) {
                        bool ok = SDL_SetGamepadLED(pad, 0, 0, 0);
                        printf("led off: %s\n", ok ? ANSI_GREEN "OK" ANSI_RESET : SDL_GetError());
                    }
                    break;
                case 'p':
                    if (pad) {
                        player_idx = (player_idx + 1) % 4;
                        bool ok = SDL_SetGamepadPlayerIndex(pad, player_idx);
                        printf("player idx -> %d: %s\n", player_idx,
                               ok ? ANSI_GREEN "OK" ANSI_RESET : SDL_GetError());
                    }
                    break;
                case '?':
                    help();
                    break;
                case '\n': case '\r':
                    break;
                default:
                    printf("unknown key '%c' — press '?' for help\n", c);
                    break;
            }
        }

        SDL_Delay(5);
    }

    printf(ANSI_DIM "cleaning up...\n" ANSI_RESET);
    if (pad) SDL_CloseGamepad(pad);
    if (joy) SDL_CloseJoystick(joy);
    SDL_Quit();
    return 0;
}
