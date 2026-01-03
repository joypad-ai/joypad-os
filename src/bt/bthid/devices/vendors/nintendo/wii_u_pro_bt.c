// wii_u_pro_bt.c - Nintendo Wii U Pro Controller Bluetooth Driver
//
// The Wii U Pro Controller uses the Wiimote extension protocol over Bluetooth.
// Device name: "Nintendo RVL-CNT-01-UC"
//
// References:
// - USB_Host_Shield_2.0/Wii.cpp
// - BlueRetro wii.c
// - https://wiibrew.org/wiki/Wiimote/Extension_Controllers/Wii_U_Pro_Controller

#include "wii_u_pro_bt.h"
#include "bt/bthid/bthid.h"
#include "bt/transport/bt_transport.h"
#include "bt/btstack/btstack_host.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include <string.h>
#include <stdio.h>
#include "pico/time.h"

// Delay before sending init commands (ms)
#define WII_U_INIT_DELAY_MS     100
#define WII_U_INIT_MAX_RETRIES  5
#define WII_U_KEEPALIVE_MS      30000  // Send status request every 30 seconds to keep alive

// ============================================================================
// WII U PRO CONSTANTS
// ============================================================================

// Button bits (after inverting - buttons report 0 when pressed)
// Derived from USB Host Shield WII_PROCONTROLLER_BUTTONS
#define WIIU_BTN_R      0x00002
#define WIIU_BTN_PLUS   0x00004
#define WIIU_BTN_HOME   0x00008
#define WIIU_BTN_MINUS  0x00010
#define WIIU_BTN_L      0x00020
#define WIIU_BTN_DOWN   0x00040
#define WIIU_BTN_RIGHT  0x00080
#define WIIU_BTN_UP     0x00100
#define WIIU_BTN_LEFT   0x00200
#define WIIU_BTN_ZR     0x00400
#define WIIU_BTN_X      0x00800
#define WIIU_BTN_A      0x01000
#define WIIU_BTN_Y      0x02000
#define WIIU_BTN_B      0x04000
#define WIIU_BTN_ZL     0x08000
#define WIIU_BTN_R3     0x10000
#define WIIU_BTN_L3     0x20000

// Stick center and range
#define WIIU_STICK_CENTER   2048
#define WIIU_STICK_RANGE    1200  // Approximate usable range from center

// Report IDs (Wiimote extension reports)
#define WIIU_REPORT_STATUS      0x20
#define WIIU_REPORT_READ_DATA   0x21
#define WIIU_REPORT_ACK         0x22  // ACK: buttons(2) + report_acked(1) + error(1)
#define WIIU_REPORT_EXT_8       0x32  // Core buttons + 8 extension bytes
#define WIIU_REPORT_EXT_19      0x34  // Core buttons + 19 extension bytes
#define WIIU_REPORT_EXT_16      0x35  // Core buttons + accel + 16 extension bytes
#define WIIU_REPORT_EXT_21      0x3D  // 21 extension bytes only (no core buttons, no accel)

// Output report IDs (sent to controller)
#define WIIU_CMD_LED            0x11
#define WIIU_CMD_REPORT_MODE    0x12
#define WIIU_CMD_STATUS_REQ     0x15  // Request status report
#define WIIU_CMD_WRITE_DATA     0x16  // Write to memory/register
#define WIIU_CMD_READ_DATA      0x17  // Read from memory/register

// ============================================================================
// DRIVER DATA
// ============================================================================

// USB Host Shield approach: status request wakes up the controller
typedef enum {
    WII_U_STATE_IDLE,
    WII_U_STATE_WAIT_INIT,          // Waiting initial delay before init
    WII_U_STATE_SEND_STATUS_REQ,    // Send status request to wake controller
    WII_U_STATE_WAIT_STATUS,        // Wait for status response
    WII_U_STATE_SEND_EXT_INIT1,     // Extension init: write 0x55 to 0xA400F0
    WII_U_STATE_WAIT_EXT_INIT1_ACK, // Wait for write ACK
    WII_U_STATE_SEND_EXT_INIT2,     // Extension init: write 0x00 to 0xA400FB
    WII_U_STATE_WAIT_EXT_INIT2_ACK, // Wait for write ACK
    WII_U_STATE_READ_EXT_TYPE,      // Read extension type from 0xA400FA
    WII_U_STATE_WAIT_EXT_TYPE,      // Wait for read response
    WII_U_STATE_SEND_REPORT_MODE,   // Send report mode
    WII_U_STATE_WAIT_REPORT_ACK,    // Wait for ACK of report mode
    WII_U_STATE_SEND_LED,           // Send LED command
    WII_U_STATE_WAIT_LED_ACK,       // Wait for ACK of LED
    WII_U_STATE_READY               // Fully initialized and receiving reports
} wii_u_state_t;

typedef struct {
    input_event_t event;
    bool initialized;
    uint8_t player_led;
    bool rumble_on;
    wii_u_state_t state;
    uint32_t init_time;         // Time when we should try init
    uint8_t init_retries;
    uint32_t last_keepalive;    // Last keepalive time (for periodic status requests)
    uint32_t last_report;       // Last data report time (for debugging)
} wii_u_pro_data_t;

static wii_u_pro_data_t wii_u_data[BTHID_MAX_DEVICES];

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Scale 16-bit stick value (center ~2048) to 8-bit (center 128)
static uint8_t scale_stick(uint16_t val)
{
    // Clamp to valid range
    if (val < WIIU_STICK_CENTER - WIIU_STICK_RANGE) {
        val = WIIU_STICK_CENTER - WIIU_STICK_RANGE;
    }
    if (val > WIIU_STICK_CENTER + WIIU_STICK_RANGE) {
        val = WIIU_STICK_CENTER + WIIU_STICK_RANGE;
    }

    // Map from [center-range, center+range] to [0, 255]
    int32_t centered = (int32_t)val - WIIU_STICK_CENTER;
    int32_t scaled = (centered * 127) / WIIU_STICK_RANGE + 128;

    if (scaled < 1) scaled = 1;
    if (scaled > 255) scaled = 255;

    return (uint8_t)scaled;
}

// Set LEDs on the controller using raw pattern (bits 4-7 = LEDs 1-4)
static void wii_u_set_leds_raw(bthid_device_t* device, uint8_t led_pattern)
{
    // LED command: report 0x11, LEDs in bits 4-7
    // Wiimote protocol: 0xA2 (DATA|OUTPUT) + report_id + data
    // Send on INTERRUPT channel (some controllers reject commands on CONTROL channel)
    uint8_t buf[3] = { 0xA2, 0x11, led_pattern };
    btstack_wiimote_send_raw(device->conn_index, buf, sizeof(buf));
}

// Set LEDs on the controller (1-4 player indicators)
static void wii_u_set_leds(bthid_device_t* device, uint8_t player)
{
    uint8_t led_pattern = 0;
    if (player >= 1 && player <= 4) {
        led_pattern = (1 << (player + 3));  // LED1=0x10, LED2=0x20, etc.
    }
    wii_u_set_leds_raw(device, led_pattern);
}

// Set rumble on/off
// Report 0x10: rumble only, bit 0 = on/off
static void wii_u_set_rumble(bthid_device_t* device, bool on)
{
    uint8_t buf[3] = { 0xA2, 0x10, on ? 0x01 : 0x00 };
    btstack_wiimote_send_raw(device->conn_index, buf, sizeof(buf));
}

// Request status report (report 0x15)
static bool wii_u_request_status(bthid_device_t* device)
{
    // Report 0x15: Request Status
    // Format: 0xA2 0x15 RR (RR = rumble bit in bit 0)
    // Send on INTERRUPT channel (some controllers reject commands on CONTROL channel)
    uint8_t buf[3] = { 0xA2, WIIU_CMD_STATUS_REQ, 0x00 };
    return btstack_wiimote_send_raw(device->conn_index, buf, sizeof(buf));
}

// Read data from controller memory/register (report 0x17)
// Used for reading extension type
static bool wii_u_read_data(bthid_device_t* device, uint32_t address, uint16_t size)
{
    // Report 0x17: Read Data
    // Format: 0xA2 0x17 MM AA AA AA SS SS
    // MM = 0x04 (extension register address space)
    // AA AA AA = 24-bit address
    // SS SS = size (16-bit big-endian)
    uint8_t buf[8];
    buf[0] = 0xA2;  // DATA|OUTPUT
    buf[1] = WIIU_CMD_READ_DATA;  // 0x17
    buf[2] = 0x04;  // Address space select (extension register)
    buf[3] = (uint8_t)((address >> 16) & 0xFF);
    buf[4] = (uint8_t)((address >> 8) & 0xFF);
    buf[5] = (uint8_t)(address & 0xFF);
    buf[6] = (uint8_t)((size >> 8) & 0xFF);  // Size high byte
    buf[7] = (uint8_t)(size & 0xFF);         // Size low byte

    printf("[WII_U_PRO] Read %d bytes from 0x%06lX\n", size, (unsigned long)address);
    return btstack_wiimote_send_raw(device->conn_index, buf, sizeof(buf));
}

// Write data to controller memory/register (report 0x16)
// Used for extension initialization
static bool wii_u_write_data(bthid_device_t* device, uint32_t address, uint8_t data)
{
    // Report 0x16: Write Data
    // Format: 0xA2 0x16 MM AA AA AA SS DD ... (padded to 23 bytes total)
    // MM = 0x04 (extension register address space)
    // AA AA AA = 24-bit address
    // SS = size (bytes to write)
    // DD = data bytes
    uint8_t buf[23];
    memset(buf, 0, sizeof(buf));

    buf[0] = 0xA2;  // DATA|OUTPUT
    buf[1] = WIIU_CMD_WRITE_DATA;  // 0x16
    buf[2] = 0x04;  // Address space select (extension register)
    buf[3] = (uint8_t)((address >> 16) & 0xFF);
    buf[4] = (uint8_t)((address >> 8) & 0xFF);
    buf[5] = (uint8_t)(address & 0xFF);
    buf[6] = 0x01;  // Size = 1 byte
    buf[7] = data;

    printf("[WII_U_PRO] Write 0x%02X to 0x%06lX\n", data, (unsigned long)address);
    return btstack_wiimote_send_raw(device->conn_index, buf, sizeof(buf));
}

// Request extension data report mode
static void wii_u_set_report_mode(bthid_device_t* device)
{
    // Try 0x3d (21 extension bytes only) on INTERRUPT channel
    // Report 0x12: Set data reporting mode
    // Wiimote protocol: 0xA2 (DATA|OUTPUT) + report_id + flags + mode
    uint8_t buf[4] = { 0xA2, 0x12, 0x04, 0x3d };  // 0x04 = continuous, 0x3d = 21 ext bytes only
    printf("[WII_U_PRO] Setting report mode 0x3D (21 ext bytes, continuous) on INTERRUPT channel\n");
    bool result = btstack_wiimote_send_raw(device->conn_index, buf, sizeof(buf));
    printf("[WII_U_PRO] set_report_mode result: %d\n", result);
}

// ============================================================================
// DRIVER IMPLEMENTATION
// ============================================================================

static bool wii_u_match(const char* device_name, const uint8_t* class_of_device,
                        uint16_t vendor_id, uint16_t product_id)
{
    (void)class_of_device;

    // Match by VID/PID
    // Nintendo VID = 0x057E, Wii U Pro PID = 0x0330
    if (vendor_id == 0x057E && product_id == 0x0330) {
        return true;
    }

    // Match by device name
    if (device_name && strstr(device_name, "Nintendo RVL-CNT-01-UC") != NULL) {
        return true;
    }

    return false;
}

static bool wii_u_init(bthid_device_t* device)
{
    printf("[WII_U_PRO] Init: %s\n", device->name);

    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (!wii_u_data[i].initialized) {
            init_input_event(&wii_u_data[i].event);
            wii_u_data[i].initialized = true;
            wii_u_data[i].player_led = 0;

            wii_u_data[i].event.type = INPUT_TYPE_GAMEPAD;
            wii_u_data[i].event.dev_addr = device->conn_index;
            wii_u_data[i].event.instance = 0;
            wii_u_data[i].event.button_count = 14;

            device->driver_data = &wii_u_data[i];

            // Defer init commands to task() to avoid ACL buffer full errors
            // Start with initial delay, then send commands one at a time
            wii_u_data[i].state = WII_U_STATE_WAIT_INIT;
            wii_u_data[i].init_time = time_us_32() + (WII_U_INIT_DELAY_MS * 1000);
            wii_u_data[i].init_retries = 0;

            printf("[WII_U_PRO] Init started, waiting %d ms before sending commands\n", WII_U_INIT_DELAY_MS);

            return true;
        }
    }

    return false;
}

static void wii_u_process_report(bthid_device_t* device, const uint8_t* data, uint16_t len)
{
    wii_u_pro_data_t* wii = (wii_u_pro_data_t*)device->driver_data;
    if (!wii || len < 1) return;

    uint8_t report_id = data[0];

    // Handle extension reports (0x32, 0x34, 0x35, 0x3D)
    // Extension data for Wii U Pro:
    //   Bytes 0-7: Sticks (4x 16-bit LE: LX, RX, LY, RY)
    //   Bytes 8-10: Buttons (inverted)

    if (report_id == WIIU_REPORT_EXT_21 && len >= 22) {
        // Report 0x3D: 21 extension bytes only (no core buttons, no accel)
        // This is the mode that works for Wii U Pro Controller
        if (wii->last_report == 0) {
            printf("[WII_U_PRO] First data report (0x3D)\n");
            // Data report arrived - make sure we send LED before going to READY
            if (wii->state < WII_U_STATE_WAIT_LED_ACK) {
                // Haven't sent LED yet - do it now
                printf("[WII_U_PRO] Data arrived before LED sent, sending LED now\n");
                wii->state = WII_U_STATE_SEND_LED;
            } else if (wii->state == WII_U_STATE_WAIT_LED_ACK) {
                // Already sent LED, waiting for ACK - go to READY
                printf("[WII_U_PRO] LED already sent, controller ready!\n");
                wii->state = WII_U_STATE_READY;
            }
            // If already READY, don't change state
        }
        wii->last_report = time_us_32();

        // Extension data starts at byte 1 (after report ID)
        const uint8_t* ext = &data[1];

        // Parse sticks (16-bit little endian)
        uint16_t lx = ext[0] | (ext[1] << 8);
        uint16_t rx = ext[2] | (ext[3] << 8);
        uint16_t ly = ext[4] | (ext[5] << 8);
        uint16_t ry = ext[6] | (ext[7] << 8);

        // Parse buttons (inverted - 0 = pressed)
        // Bytes 8-10 contain button state
        uint32_t raw_buttons = ((uint32_t)(ext[8] & 0xFE)) |
                               ((uint32_t)ext[9] << 8) |
                               ((uint32_t)(ext[10] & 0x03) << 16);
        uint32_t buttons_pressed = ~raw_buttons;

        // Map to JP buttons
        uint32_t buttons = 0x00000000;

        // Face buttons (Nintendo layout: A=right, B=bottom, X=top, Y=left)
        if (buttons_pressed & WIIU_BTN_B)     buttons |= JP_BUTTON_B1;  // Bottom
        if (buttons_pressed & WIIU_BTN_A)     buttons |= JP_BUTTON_B2;  // Right
        if (buttons_pressed & WIIU_BTN_Y)     buttons |= JP_BUTTON_B3;  // Left
        if (buttons_pressed & WIIU_BTN_X)     buttons |= JP_BUTTON_B4;  // Top

        // Shoulder buttons
        if (buttons_pressed & WIIU_BTN_L)     buttons |= JP_BUTTON_L1;
        if (buttons_pressed & WIIU_BTN_R)     buttons |= JP_BUTTON_R1;
        if (buttons_pressed & WIIU_BTN_ZL)    buttons |= JP_BUTTON_L2;
        if (buttons_pressed & WIIU_BTN_ZR)    buttons |= JP_BUTTON_R2;

        // System buttons
        if (buttons_pressed & WIIU_BTN_MINUS) buttons |= JP_BUTTON_S1;
        if (buttons_pressed & WIIU_BTN_PLUS)  buttons |= JP_BUTTON_S2;
        if (buttons_pressed & WIIU_BTN_L3)    buttons |= JP_BUTTON_L3;
        if (buttons_pressed & WIIU_BTN_R3)    buttons |= JP_BUTTON_R3;
        if (buttons_pressed & WIIU_BTN_HOME)  buttons |= JP_BUTTON_A1;

        // D-pad
        if (buttons_pressed & WIIU_BTN_UP)    buttons |= JP_BUTTON_DU;
        if (buttons_pressed & WIIU_BTN_DOWN)  buttons |= JP_BUTTON_DD;
        if (buttons_pressed & WIIU_BTN_LEFT)  buttons |= JP_BUTTON_DL;
        if (buttons_pressed & WIIU_BTN_RIGHT) buttons |= JP_BUTTON_DR;

        wii->event.buttons = buttons;

        // Scale sticks to 8-bit
        // Y axes are inverted on Wii U Pro (up = higher value)
        wii->event.analog[ANALOG_X] = scale_stick(lx);
        wii->event.analog[ANALOG_Y] = 255 - scale_stick(ly);  // Invert Y
        wii->event.analog[ANALOG_Z] = scale_stick(rx);
        wii->event.analog[ANALOG_RX] = 255 - scale_stick(ry); // Invert Y

        router_submit_input(&wii->event);

    } else if (report_id == WIIU_REPORT_EXT_16 && len >= 22) {
        // Report 0x35: Core buttons (2) + Accel (3) + Extension (16) = 21 bytes total
        if (wii->last_report == 0) {
            printf("[WII_U_PRO] First data report (0x35)\n");
            // Data report arrived - make sure we send LED before going to READY
            if (wii->state < WII_U_STATE_WAIT_LED_ACK) {
                // Haven't sent LED yet - do it now
                printf("[WII_U_PRO] Data arrived before LED sent, sending LED now\n");
                wii->state = WII_U_STATE_SEND_LED;
            } else if (wii->state == WII_U_STATE_WAIT_LED_ACK) {
                // Already sent LED, waiting for ACK - go to READY
                printf("[WII_U_PRO] LED already sent, controller ready!\n");
                wii->state = WII_U_STATE_READY;
            }
        }
        wii->last_report = time_us_32();

        // Report 0x35: bytes 1-2 = core buttons, 3-5 = accel, 6-21 = extension
        const uint8_t* ext = &data[6];  // Extension data starts at byte 6

        // Parse sticks (16-bit little endian)
        uint16_t lx = ext[0] | (ext[1] << 8);
        uint16_t rx = ext[2] | (ext[3] << 8);
        uint16_t ly = ext[4] | (ext[5] << 8);
        uint16_t ry = ext[6] | (ext[7] << 8);

        // Parse buttons (inverted - 0 = pressed)
        // Bytes 8-10 contain button state
        uint32_t raw_buttons = ((uint32_t)(ext[8] & 0xFE)) |
                               ((uint32_t)ext[9] << 8) |
                               ((uint32_t)(ext[10] & 0x03) << 16);
        uint32_t buttons_pressed = ~raw_buttons;

        // Map to JP buttons
        uint32_t buttons = 0x00000000;

        // Face buttons (Nintendo layout: A=right, B=bottom, X=top, Y=left)
        if (buttons_pressed & WIIU_BTN_B)     buttons |= JP_BUTTON_B1;  // Bottom
        if (buttons_pressed & WIIU_BTN_A)     buttons |= JP_BUTTON_B2;  // Right
        if (buttons_pressed & WIIU_BTN_Y)     buttons |= JP_BUTTON_B3;  // Left
        if (buttons_pressed & WIIU_BTN_X)     buttons |= JP_BUTTON_B4;  // Top

        // Shoulder buttons
        if (buttons_pressed & WIIU_BTN_L)     buttons |= JP_BUTTON_L1;
        if (buttons_pressed & WIIU_BTN_R)     buttons |= JP_BUTTON_R1;
        if (buttons_pressed & WIIU_BTN_ZL)    buttons |= JP_BUTTON_L2;
        if (buttons_pressed & WIIU_BTN_ZR)    buttons |= JP_BUTTON_R2;

        // System buttons
        if (buttons_pressed & WIIU_BTN_MINUS) buttons |= JP_BUTTON_S1;
        if (buttons_pressed & WIIU_BTN_PLUS)  buttons |= JP_BUTTON_S2;
        if (buttons_pressed & WIIU_BTN_L3)    buttons |= JP_BUTTON_L3;
        if (buttons_pressed & WIIU_BTN_R3)    buttons |= JP_BUTTON_R3;
        if (buttons_pressed & WIIU_BTN_HOME)  buttons |= JP_BUTTON_A1;

        // D-pad
        if (buttons_pressed & WIIU_BTN_UP)    buttons |= JP_BUTTON_DU;
        if (buttons_pressed & WIIU_BTN_DOWN)  buttons |= JP_BUTTON_DD;
        if (buttons_pressed & WIIU_BTN_LEFT)  buttons |= JP_BUTTON_DL;
        if (buttons_pressed & WIIU_BTN_RIGHT) buttons |= JP_BUTTON_DR;

        wii->event.buttons = buttons;

        // Scale sticks to 8-bit
        // Y axes are inverted on Wii U Pro (up = higher value)
        wii->event.analog[ANALOG_X] = scale_stick(lx);
        wii->event.analog[ANALOG_Y] = 255 - scale_stick(ly);  // Invert Y
        wii->event.analog[ANALOG_Z] = scale_stick(rx);
        wii->event.analog[ANALOG_RX] = 255 - scale_stick(ry); // Invert Y

        router_submit_input(&wii->event);

    } else if (report_id == WIIU_REPORT_EXT_19 && len >= 22) {
        // We got a valid report - transition to READY state
        if (wii->state != WII_U_STATE_READY) {
            printf("[WII_U_PRO] Received first data report (0x34), controller ready!\n");
            wii->state = WII_U_STATE_READY;
        }

        // Report 0x34: Core buttons (2) + 19 extension bytes
        const uint8_t* ext = &data[3];  // Extension starts at byte 3

        // Same parsing as 0x35
        uint16_t lx = ext[0] | (ext[1] << 8);
        uint16_t rx = ext[2] | (ext[3] << 8);
        uint16_t ly = ext[4] | (ext[5] << 8);
        uint16_t ry = ext[6] | (ext[7] << 8);

        uint32_t raw_buttons = ((uint32_t)(ext[8] & 0xFE)) |
                               ((uint32_t)ext[9] << 8) |
                               ((uint32_t)(ext[10] & 0x03) << 16);
        uint32_t buttons_pressed = ~raw_buttons;

        uint32_t buttons = 0x00000000;

        if (buttons_pressed & WIIU_BTN_B)     buttons |= JP_BUTTON_B1;  // Bottom
        if (buttons_pressed & WIIU_BTN_A)     buttons |= JP_BUTTON_B2;  // Right
        if (buttons_pressed & WIIU_BTN_Y)     buttons |= JP_BUTTON_B3;  // Left
        if (buttons_pressed & WIIU_BTN_X)     buttons |= JP_BUTTON_B4;  // Top
        if (buttons_pressed & WIIU_BTN_L)     buttons |= JP_BUTTON_L1;
        if (buttons_pressed & WIIU_BTN_R)     buttons |= JP_BUTTON_R1;
        if (buttons_pressed & WIIU_BTN_ZL)    buttons |= JP_BUTTON_L2;
        if (buttons_pressed & WIIU_BTN_ZR)    buttons |= JP_BUTTON_R2;
        if (buttons_pressed & WIIU_BTN_MINUS) buttons |= JP_BUTTON_S1;
        if (buttons_pressed & WIIU_BTN_PLUS)  buttons |= JP_BUTTON_S2;
        if (buttons_pressed & WIIU_BTN_L3)    buttons |= JP_BUTTON_L3;
        if (buttons_pressed & WIIU_BTN_R3)    buttons |= JP_BUTTON_R3;
        if (buttons_pressed & WIIU_BTN_HOME)  buttons |= JP_BUTTON_A1;
        if (buttons_pressed & WIIU_BTN_UP)    buttons |= JP_BUTTON_DU;
        if (buttons_pressed & WIIU_BTN_DOWN)  buttons |= JP_BUTTON_DD;
        if (buttons_pressed & WIIU_BTN_LEFT)  buttons |= JP_BUTTON_DL;
        if (buttons_pressed & WIIU_BTN_RIGHT) buttons |= JP_BUTTON_DR;

        wii->event.buttons = buttons;
        wii->event.analog[ANALOG_X] = scale_stick(lx);
        wii->event.analog[ANALOG_Y] = 255 - scale_stick(ly);
        wii->event.analog[ANALOG_Z] = scale_stick(rx);
        wii->event.analog[ANALOG_RX] = 255 - scale_stick(ry);

        router_submit_input(&wii->event);

    } else if (report_id == WIIU_REPORT_STATUS && len >= 7) {
        // Status report: byte 1-2 = buttons, byte 3 = flags, byte 4 = LEDs, byte 5-6 = battery
        uint8_t flags = data[3];
        bool extension_connected = (flags & 0x02) != 0;

        // Only log during init, not during keepalive
        if (wii->state == WII_U_STATE_WAIT_STATUS) {
            printf("[WII_U_PRO] Status report: flags=0x%02X ext=%d\n", flags, extension_connected);
            if (extension_connected) {
                printf("[WII_U_PRO] Extension connected, doing full init sequence\n");
                wii->state = WII_U_STATE_SEND_EXT_INIT1;
            } else {
                printf("[WII_U_PRO] No extension, going to LED\n");
                wii->state = WII_U_STATE_SEND_LED;
            }
        }

    } else if (report_id == WIIU_REPORT_ACK && len >= 5) {
        // ACK report: byte 1-2 = buttons, byte 3 = report_id acked, byte 4 = error code
        uint8_t acked_report = data[3];
        uint8_t error_code = data[4];
        printf("[WII_U_PRO] ACK: report=0x%02X error=%d state=%d\n", acked_report, error_code, wii->state);

        // USB Host Shield sequence: status → ext init → read ext type → report mode → LED
        if (error_code == 0) {
            if (wii->state == WII_U_STATE_WAIT_EXT_INIT1_ACK && acked_report == WIIU_CMD_WRITE_DATA) {
                printf("[WII_U_PRO] Ext init 1 ACK, sending init 2\n");
                wii->state = WII_U_STATE_SEND_EXT_INIT2;
            } else if (wii->state == WII_U_STATE_WAIT_EXT_INIT2_ACK && acked_report == WIIU_CMD_WRITE_DATA) {
                printf("[WII_U_PRO] Ext init 2 ACK, reading extension type\n");
                wii->state = WII_U_STATE_READ_EXT_TYPE;
            } else if (wii->state == WII_U_STATE_WAIT_REPORT_ACK && acked_report == WIIU_CMD_REPORT_MODE) {
                printf("[WII_U_PRO] Report mode ACK, sending LED\n");
                wii->state = WII_U_STATE_SEND_LED;
            } else if (wii->state == WII_U_STATE_WAIT_LED_ACK && acked_report == WIIU_CMD_LED) {
                printf("[WII_U_PRO] LED ACK received, init complete! Waiting for data reports...\n");
                wii->state = WII_U_STATE_READY;
                wii->last_keepalive = time_us_32();
                wii->last_report = 0;
            }
        } else {
            printf("[WII_U_PRO] ACK error %d for report 0x%02X\n", error_code, acked_report);
        }

    } else if (report_id == WIIU_REPORT_READ_DATA && len >= 7) {
        // Read data response (0x21): byte 3 = SE (bits 7-4=size-1, bits 3-0=error)
        // bytes 4-5 = address, bytes 6+ = data
        uint8_t size_error = data[3];
        uint8_t size = ((size_error >> 4) & 0x0F) + 1;  // bits 7-4 = size-1
        uint8_t error = size_error & 0x0F;              // bits 3-0 = error

        printf("[WII_U_PRO] Read response: size=%d error=%d state=%d\n", size, error, wii->state);

        if (wii->state == WII_U_STATE_WAIT_EXT_TYPE) {
            if (error == 0 && len >= 12) {
                // Extension type bytes at data[6..11]: should be 00 A4 20 01 20 for Wii U Pro
                printf("[WII_U_PRO] Extension type: %02X %02X %02X %02X %02X %02X\n",
                       data[6], data[7], data[8], data[9], data[10], data[11]);

                // Check for Wii U Pro signature: 00 00 A4 20 01 20
                if (data[6] == 0x00 && data[7] == 0x00 && data[8] == 0xA4 &&
                    data[9] == 0x20 && data[10] == 0x01 && data[11] == 0x20) {
                    printf("[WII_U_PRO] Wii U Pro Controller confirmed!\n");
                }
            } else {
                printf("[WII_U_PRO] Extension type read error=%d\n", error);
            }
            // Set report mode on INTERRUPT channel (like USB Host Shield for motionPlusInside)
            printf("[WII_U_PRO] Extension init done, setting report mode\n");
            wii->state = WII_U_STATE_SEND_REPORT_MODE;
        }
    }
}

static void wii_u_task(bthid_device_t* device)
{
    wii_u_pro_data_t* wii = (wii_u_pro_data_t*)device->driver_data;
    if (!wii) return;

    uint32_t now = time_us_32();

    // USB Host Shield sequence: status → ext init → report mode → LED
    switch (wii->state) {
        case WII_U_STATE_WAIT_INIT:
            // Wait for initial delay, then send status request
            if ((int32_t)(now - wii->init_time) >= 0) {
                printf("[WII_U_PRO] Init delay complete, sending status request\n");
                wii->state = WII_U_STATE_SEND_STATUS_REQ;
            }
            break;

        case WII_U_STATE_SEND_STATUS_REQ:
            if (btstack_wiimote_can_send(device->conn_index)) {
                printf("[WII_U_PRO] Sending status request\n");
                wii_u_request_status(device);
                wii->state = WII_U_STATE_WAIT_STATUS;
                wii->init_time = now + (1000 * 1000);  // 1 second timeout
            }
            break;

        case WII_U_STATE_WAIT_STATUS:
            // Status response handled in process_report
            if ((int32_t)(now - wii->init_time) >= 0) {
                wii->init_retries++;
                if (wii->init_retries < WII_U_INIT_MAX_RETRIES) {
                    printf("[WII_U_PRO] Status timeout, retry %d\n", wii->init_retries);
                    wii->state = WII_U_STATE_SEND_STATUS_REQ;
                } else {
                    printf("[WII_U_PRO] Status failed, trying ext init anyway\n");
                    wii->init_retries = 0;
                    wii->state = WII_U_STATE_SEND_EXT_INIT1;
                }
            }
            break;

        case WII_U_STATE_SEND_EXT_INIT1:
            // Standard extension init: write 0x55 to 0xA400F0
            if (btstack_wiimote_can_send(device->conn_index)) {
                printf("[WII_U_PRO] Ext init 1: 0x55 to 0xA400F0\n");
                wii_u_write_data(device, 0xA400F0, 0x55);
                wii->state = WII_U_STATE_WAIT_EXT_INIT1_ACK;
                wii->init_time = now + (1000 * 1000);
            }
            break;

        case WII_U_STATE_WAIT_EXT_INIT1_ACK:
            if ((int32_t)(now - wii->init_time) >= 0) {
                wii->init_retries++;
                if (wii->init_retries < WII_U_INIT_MAX_RETRIES) {
                    printf("[WII_U_PRO] Ext init 1 timeout, retry %d\n", wii->init_retries);
                    wii->state = WII_U_STATE_SEND_EXT_INIT1;
                } else {
                    printf("[WII_U_PRO] Ext init 1 failed, trying init 2 anyway\n");
                    wii->init_retries = 0;
                    wii->state = WII_U_STATE_SEND_EXT_INIT2;
                }
            }
            break;

        case WII_U_STATE_SEND_EXT_INIT2:
            // Standard extension init: write 0x00 to 0xA400FB
            if (btstack_wiimote_can_send(device->conn_index)) {
                printf("[WII_U_PRO] Ext init 2: 0x00 to 0xA400FB\n");
                wii_u_write_data(device, 0xA400FB, 0x00);
                wii->state = WII_U_STATE_WAIT_EXT_INIT2_ACK;
                wii->init_time = now + (1000 * 1000);
            }
            break;

        case WII_U_STATE_WAIT_EXT_INIT2_ACK:
            if ((int32_t)(now - wii->init_time) >= 0) {
                wii->init_retries++;
                if (wii->init_retries < WII_U_INIT_MAX_RETRIES) {
                    printf("[WII_U_PRO] Ext init 2 timeout, retry %d\n", wii->init_retries);
                    wii->state = WII_U_STATE_SEND_EXT_INIT2;
                } else {
                    printf("[WII_U_PRO] Ext init 2 failed, trying ext type read anyway\n");
                    wii->init_retries = 0;
                    wii->state = WII_U_STATE_READ_EXT_TYPE;
                }
            }
            break;

        case WII_U_STATE_READ_EXT_TYPE:
            if (btstack_wiimote_can_send(device->conn_index)) {
                printf("[WII_U_PRO] Reading extension type (0xA400FA)\n");
                wii_u_read_data(device, 0xA400FA, 6);
                wii->state = WII_U_STATE_WAIT_EXT_TYPE;
                wii->init_time = now + (1000 * 1000);
            }
            break;

        case WII_U_STATE_WAIT_EXT_TYPE:
            // Read response handled in process_report
            if ((int32_t)(now - wii->init_time) >= 0) {
                wii->init_retries++;
                if (wii->init_retries < WII_U_INIT_MAX_RETRIES) {
                    printf("[WII_U_PRO] Ext type read timeout, retry %d\n", wii->init_retries);
                    wii->state = WII_U_STATE_READ_EXT_TYPE;
                } else {
                    printf("[WII_U_PRO] Ext type read failed, trying report mode anyway\n");
                    wii->init_retries = 0;
                    wii->state = WII_U_STATE_SEND_REPORT_MODE;
                }
            }
            break;

        case WII_U_STATE_SEND_REPORT_MODE:
            if (btstack_wiimote_can_send(device->conn_index)) {
                printf("[WII_U_PRO] Sending report mode command\n");
                wii_u_set_report_mode(device);
                wii->state = WII_U_STATE_WAIT_REPORT_ACK;
                wii->init_time = now + (1000 * 1000);
            }
            break;

        case WII_U_STATE_WAIT_REPORT_ACK:
            if ((int32_t)(now - wii->init_time) >= 0) {
                wii->init_retries++;
                if (wii->init_retries < WII_U_INIT_MAX_RETRIES) {
                    printf("[WII_U_PRO] Report mode timeout, retry %d\n", wii->init_retries);
                    wii->state = WII_U_STATE_SEND_REPORT_MODE;
                } else {
                    printf("[WII_U_PRO] Report mode failed, trying LED anyway\n");
                    wii->init_retries = 0;
                    wii->state = WII_U_STATE_SEND_LED;
                }
            }
            break;

        case WII_U_STATE_SEND_LED:
            if (btstack_wiimote_can_send(device->conn_index)) {
                printf("[WII_U_PRO] Sending LED command\n");
                wii->player_led = 0x10;  // LED1 = bit 4
                wii_u_set_leds(device, 1);
                wii->state = WII_U_STATE_WAIT_LED_ACK;
                wii->init_time = now + (1000 * 1000);
            }
            break;

        case WII_U_STATE_WAIT_LED_ACK:
            if ((int32_t)(now - wii->init_time) >= 0) {
                wii->init_retries++;
                if (wii->init_retries < WII_U_INIT_MAX_RETRIES) {
                    printf("[WII_U_PRO] LED timeout, retry %d\n", wii->init_retries);
                    wii->state = WII_U_STATE_SEND_LED;
                } else {
                    printf("[WII_U_PRO] LED failed, continuing anyway\n");
                    wii->init_retries = 0;
                    wii->state = WII_U_STATE_READY;
                    wii->last_keepalive = now;
                    wii->last_report = 0;
                }
            }
            break;

        case WII_U_STATE_READY:
            // Monitor feedback system for LED and rumble changes
            {
                int player_idx = find_player_index(wii->event.dev_addr, wii->event.instance);
                if (player_idx >= 0) {
                    feedback_state_t* fb = feedback_get_state(player_idx);

                    // Check rumble from feedback system
                    if (fb->rumble_dirty) {
                        bool rumble_wanted = (fb->rumble.left > 0 || fb->rumble.right > 0);
                        if (rumble_wanted != wii->rumble_on) {
                            if (btstack_wiimote_can_send(device->conn_index)) {
                                wii->rumble_on = rumble_wanted;
                                wii_u_set_rumble(device, rumble_wanted);
                            }
                        }
                    }

                    // Check LED from feedback system
                    // Feedback pattern: bits 0-3 for players 1-4 (0x01, 0x02, 0x04, 0x08)
                    // Wii U Pro LED: bits 4-7 for LEDs 1-4 (0x10, 0x20, 0x40, 0x80)
                    // Conversion: shift left by 4
                    uint8_t led;
                    if (fb->led.pattern != 0) {
                        // Use LED from host/feedback system
                        led = fb->led.pattern << 4;
                    } else {
                        // Default to player index-based LED
                        led = PLAYER_LEDS[player_idx + 1] << 4;
                    }

                    if (fb->led_dirty || led != wii->player_led) {
                        if (btstack_wiimote_can_send(device->conn_index)) {
                            wii->player_led = led;
                            wii_u_set_leds_raw(device, led);
                        }
                    }

                    // Clear dirty flags after processing
                    if (fb->rumble_dirty || fb->led_dirty) {
                        feedback_clear_dirty(player_idx);
                    }
                }
            }

            // Send periodic status requests to keep connection alive
            if ((int32_t)(now - wii->last_keepalive) >= (WII_U_KEEPALIVE_MS * 1000)) {
                if (btstack_wiimote_can_send(device->conn_index)) {
                    wii_u_request_status(device);
                    wii->last_keepalive = now;
                }
            }
            break;

        case WII_U_STATE_IDLE:
        default:
            break;
    }
}

static void wii_u_disconnect(bthid_device_t* device)
{
    printf("[WII_U_PRO] Disconnect: %s\n", device->name);

    wii_u_pro_data_t* wii = (wii_u_pro_data_t*)device->driver_data;
    if (wii) {
        router_device_disconnected(wii->event.dev_addr, wii->event.instance);
        remove_players_by_address(wii->event.dev_addr, wii->event.instance);

        init_input_event(&wii->event);
        wii->initialized = false;
        wii->state = WII_U_STATE_IDLE;
        wii->init_retries = 0;
    }
}

// ============================================================================
// DRIVER STRUCT
// ============================================================================

const bthid_driver_t wii_u_pro_bt_driver = {
    .name = "Nintendo Wii U Pro Controller",
    .match = wii_u_match,
    .init = wii_u_init,
    .process_report = wii_u_process_report,
    .task = wii_u_task,
    .disconnect = wii_u_disconnect,
};

void wii_u_pro_bt_register(void)
{
    bthid_register_driver(&wii_u_pro_bt_driver);
}
