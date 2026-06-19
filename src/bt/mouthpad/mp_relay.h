// mp_relay.h - MouthPad NUS <-> CDC relay codec
//
// Self-contained, platform-agnostic codec that lets the Augmental desktop
// utility talk to a MouthPad through JoypadOS exactly as it does through the
// dedicated mouthpad-usb dongle. It implements the utility's wire contract:
//
//   Framing (PacketFramer): [0xAA 0x55][len_hi len_lo][payload][crc_hi crc_lo]
//     - length  : big-endian uint16 (payload byte count)
//     - crc     : CRC-16/CCITT-FALSE (init 0xFFFF, poly 0x1021) over payload, BE
//
//   Payload (protobuf, MouthpadRelay.proto) — "minimal passthrough" subset:
//     host->device : AppToRelayMessage { pass_through_to_mouthpad{ data } }  (field 3 / field 1)
//     device->host : RelayToAppMessage { pass_through_to_app{ data } }       (field 3 / field 1)
//
// The `data` bytes ARE the raw Nordic UART Service (NUS) stream. This module
// has no BTstack/USB/RTOS dependency — it is pure byte-in / byte-out so it can
// be unit-tested on a host and shared verbatim across nRF / Pico W / Pico 2 W.
// The transport glue (CDC <-> these calls, NUS <-> these calls, with SPSC ring
// buffers across execution contexts) lives in mp_bridge.c.

#ifndef MP_RELAY_H
#define MP_RELAY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// NUS MTU payload is <=247; framed worst case = 4 + ~6 proto + 247 + 2.
#define MP_RELAY_MAX_NUS_PAYLOAD   247
#define MP_RELAY_MAX_FRAME         320

// CRC-16/CCITT-FALSE over `data` (init 0xFFFF, poly 0x1021, no reflection).
uint16_t mp_relay_crc16(const uint8_t* data, size_t len);

// Encode device->host: wrap `payload` (raw NUS bytes from the MouthPad) as a
// framed RelayToAppMessage{pass_through_to_app{data}}. Writes into `out`.
// Returns total framed length, or 0 if payload too large / out_cap too small.
size_t mp_relay_encode_to_app(const uint8_t* payload, uint16_t len,
                              uint8_t* out, size_t out_cap);

// ---------------------------------------------------------------------------
// Dongle-level response encoders. The desktop utility's connection handshake is
// addressed to the relay/dongle itself (destination=relay), not the MouthPad:
// it sends device_info_read / ble_connection_status_read and gates "connected"
// on the responses. JoypadOS answers them from its live BLE link.
// ---------------------------------------------------------------------------

// RelayBleConnectionStatus (MouthpadRelay.proto)
typedef enum {
    MP_RELAY_CONN_UNSPECIFIED  = 0,
    MP_RELAY_CONN_DISCONNECTED = 1,
    MP_RELAY_CONN_CONNECTED    = 2,
    MP_RELAY_CONN_SEARCHING    = 3,
    MP_RELAY_CONN_CONNECTING   = 4,
} mp_relay_conn_status_t;

// DeviceFamily / DeviceBoard (MouthpadRelay.proto) — values we may report.
#define MP_RELAY_FAMILY_UNSPECIFIED        0
#define MP_RELAY_FAMILY_NRF                1
#define MP_RELAY_FAMILY_ESP                2
#define MP_RELAY_BOARD_UNSPECIFIED         0
#define MP_RELAY_BOARD_APRBROTHER_NRF52840 3

typedef struct {
    const char* name;       // MouthPad advertised name (may be "")
    const char* firmware;   // MouthPad firmware version string (may be "")
    const char* address;    // BLE MAC as text "AA:BB:CC:DD:EE:FF" (may be "")
    uint32_t    vid;        // DIS PnP USB vendor id
    uint32_t    pid;        // DIS PnP USB product id
    uint32_t    family;     // DeviceFamily of THIS dongle (JoypadOS)
    uint32_t    board;      // DeviceBoard of THIS dongle (JoypadOS)
} mp_relay_device_info_t;

// Encode RelayToAppMessage{ device_info_response{...} } as a framed packet.
size_t mp_relay_encode_device_info(const mp_relay_device_info_t* info,
                                   uint8_t* out, size_t out_cap);

// Encode RelayToAppMessage{ ble_connection_status_response{...} } as a framed packet.
size_t mp_relay_encode_conn_status(mp_relay_conn_status_t status, int32_t rssi,
                                   uint32_t battery, uint8_t* out, size_t out_cap);

// ---------------------------------------------------------------------------
// Host->device reconstructor: feed raw CDC bytes; for each complete, CRC-valid
// frame the callback fires with the classified request. For PASSTHROUGH the
// data/len carry the inner NUS payload (to write to the MouthPad's NUS RX); the
// dongle-level reads carry no payload (the handler synthesizes a response).
// ---------------------------------------------------------------------------
typedef enum {
    MP_RELAY_REQ_NONE = 0,
    MP_RELAY_REQ_PASSTHROUGH,        // -> write data/len to MouthPad NUS
    MP_RELAY_REQ_DEVICE_INFO_READ,   // -> answer device_info_response
    MP_RELAY_REQ_CONN_STATUS_READ,   // -> answer ble_connection_status_response
    MP_RELAY_REQ_OTHER,              // recognized but unhandled here
} mp_relay_req_t;

typedef void (*mp_relay_request_cb)(mp_relay_req_t type, const uint8_t* data,
                                    uint16_t len, void* ctx);

typedef struct {
    uint8_t  acc[MP_RELAY_MAX_FRAME];   // frame-assembly buffer
    uint16_t acc_len;                   // bytes currently buffered
    mp_relay_request_cb cb;
    void*    ctx;
} mp_relay_rx_t;

void mp_relay_rx_init(mp_relay_rx_t* rx, mp_relay_request_cb cb, void* ctx);
void mp_relay_rx_feed(mp_relay_rx_t* rx, const uint8_t* data, size_t len);

#endif // MP_RELAY_H
