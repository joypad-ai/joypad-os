// mp_relay.c - MouthPad NUS <-> CDC relay codec (see mp_relay.h)
//
// Pure byte-in / byte-out; no BTstack / USB / RTOS dependencies.

#include "mp_relay.h"
#include <string.h>

// Protobuf field keys (field_number << 3 | wire_type). wire_type 2 = len-delim.
#define PB_KEY_FIELD1_LEN   0x0A   // (1<<3)|2  -> PassThrough{ data }
#define PB_KEY_FIELD3_LEN   0x1A   // (3<<3)|2  -> {pass_through_to_mouthpad|app}

// ---------------------------------------------------------------------------
// CRC-16/CCITT-FALSE (init 0xFFFF, poly 0x1021, no reflection) — matches the
// utility's PacketFramer CRC16.calculate() byte-for-byte.
// ---------------------------------------------------------------------------
uint16_t mp_relay_crc16(const uint8_t* data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
            else              crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

// ---------------------------------------------------------------------------
// Minimal protobuf helpers
// ---------------------------------------------------------------------------
// Append a base-128 varint. Returns new write offset.
static size_t pb_put_varint(uint8_t* buf, size_t pos, uint32_t v)
{
    while (v >= 0x80) {
        buf[pos++] = (uint8_t)(v | 0x80);
        v >>= 7;
    }
    buf[pos++] = (uint8_t)v;
    return pos;
}

static size_t pb_varint_size(uint32_t v)
{
    size_t n = 1;
    while (v >= 0x80) { v >>= 7; n++; }
    return n;
}

// Read a varint with bounds checking. Returns true on success.
static bool pb_get_varint(const uint8_t* buf, size_t len, size_t* pos, uint32_t* out)
{
    uint32_t result = 0;
    int shift = 0;
    while (*pos < len && shift < 32) {
        uint8_t byte = buf[(*pos)++];
        result |= (uint32_t)(byte & 0x7F) << shift;
        if (!(byte & 0x80)) { *out = result; return true; }
        shift += 7;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Encode device->host: RelayToAppMessage{ pass_through_to_app{ data=payload } }
// ---------------------------------------------------------------------------
size_t mp_relay_encode_to_app(const uint8_t* payload, uint16_t len,
                              uint8_t* out, size_t out_cap)
{
    if (len > MP_RELAY_MAX_NUS_PAYLOAD) return 0;

    // Inner PassThroughToApp { data(field1,len-delim) = payload }
    size_t inner_len = 1 + pb_varint_size(len) + len;
    // Outer RelayToAppMessage { pass_through_to_app(field3,len-delim) = inner }
    size_t proto_len = 1 + pb_varint_size((uint32_t)inner_len) + inner_len;
    // Frame: 2 magic + 2 length + proto + 2 crc
    size_t frame_len = 4 + proto_len + 2;
    if (frame_len > out_cap) return 0;

    // Build the protobuf payload at out+4 (after the header) so we can CRC it.
    uint8_t* proto = out + 4;
    size_t p = 0;
    proto[p++] = PB_KEY_FIELD3_LEN;
    p = pb_put_varint(proto, p, (uint32_t)inner_len);
    proto[p++] = PB_KEY_FIELD1_LEN;
    p = pb_put_varint(proto, p, len);
    memcpy(proto + p, payload, len);
    p += len;
    // p == proto_len

    // Header
    out[0] = 0xAA;
    out[1] = 0x55;
    out[2] = (uint8_t)(proto_len >> 8);
    out[3] = (uint8_t)(proto_len & 0xFF);

    // CRC over the protobuf payload, big-endian
    uint16_t crc = mp_relay_crc16(proto, proto_len);
    out[4 + proto_len + 0] = (uint8_t)(crc >> 8);
    out[4 + proto_len + 1] = (uint8_t)(crc & 0xFF);

    return frame_len;
}

// ---------------------------------------------------------------------------
// Append helpers for building outgoing protobuf messages.
// ---------------------------------------------------------------------------
static size_t pb_put_bytes_field(uint8_t* buf, size_t pos, uint32_t field,
                                 const uint8_t* data, size_t len)
{
    buf[pos++] = (uint8_t)((field << 3) | 2);   // wire type 2 (len-delimited)
    pos = pb_put_varint(buf, pos, (uint32_t)len);
    memcpy(buf + pos, data, len);
    return pos + len;
}

static size_t pb_put_varint_field(uint8_t* buf, size_t pos, uint32_t field, uint32_t v)
{
    buf[pos++] = (uint8_t)((field << 3) | 0);   // wire type 0 (varint)
    return pb_put_varint(buf, pos, v);
}

// Wrap a complete protobuf payload as a framed packet (magic + BE len + crc).
static size_t frame_wrap(const uint8_t* proto, size_t proto_len, uint8_t* out, size_t out_cap)
{
    size_t frame_len = 4 + proto_len + 2;
    if (frame_len > out_cap) return 0;
    out[0] = 0xAA;
    out[1] = 0x55;
    out[2] = (uint8_t)(proto_len >> 8);
    out[3] = (uint8_t)(proto_len & 0xFF);
    memcpy(out + 4, proto, proto_len);
    uint16_t crc = mp_relay_crc16(proto, proto_len);
    out[4 + proto_len + 0] = (uint8_t)(crc >> 8);
    out[4 + proto_len + 1] = (uint8_t)(crc & 0xFF);
    return frame_len;
}

// ---------------------------------------------------------------------------
// Encode device->host RelayToAppMessage{ device_info_response{...} } (field 4)
// ---------------------------------------------------------------------------
size_t mp_relay_encode_device_info(const mp_relay_device_info_t* info,
                                   uint8_t* out, size_t out_cap)
{
    uint8_t inner[256];
    size_t ip = 0;
    if (info->name && info->name[0])
        ip = pb_put_bytes_field(inner, ip, 1, (const uint8_t*)info->name, strlen(info->name));
    if (info->firmware && info->firmware[0])
        ip = pb_put_bytes_field(inner, ip, 2, (const uint8_t*)info->firmware, strlen(info->firmware));
    if (info->address && info->address[0])
        ip = pb_put_bytes_field(inner, ip, 3, (const uint8_t*)info->address, strlen(info->address));
    if (info->vid)    ip = pb_put_varint_field(inner, ip, 4, info->vid);
    if (info->pid)    ip = pb_put_varint_field(inner, ip, 5, info->pid);
    if (info->family) ip = pb_put_varint_field(inner, ip, 6, info->family);
    if (info->board)  ip = pb_put_varint_field(inner, ip, 7, info->board);

    uint8_t proto[280];
    size_t pp = 0;
    proto[pp++] = (4 << 3) | 2;             // RelayToAppMessage.device_info_response
    pp = pb_put_varint(proto, pp, (uint32_t)ip);
    memcpy(proto + pp, inner, ip);
    pp += ip;
    return frame_wrap(proto, pp, out, out_cap);
}

// ---------------------------------------------------------------------------
// Encode device->host RelayToAppMessage{ ble_connection_status_response{...} } (field 1)
// ---------------------------------------------------------------------------
size_t mp_relay_encode_conn_status(mp_relay_conn_status_t status, int32_t rssi,
                                   uint32_t battery, uint8_t* out, size_t out_cap)
{
    uint8_t inner[24];
    size_t ip = 0;
    ip = pb_put_varint_field(inner, ip, 1, (uint32_t)status);          // connection_status
    if (rssi)    ip = pb_put_varint_field(inner, ip, 2, (uint32_t)rssi);   // int32 (small negatives ok)
    if (battery) ip = pb_put_varint_field(inner, ip, 3, battery);         // battery_level

    uint8_t proto[32];
    size_t pp = 0;
    proto[pp++] = (1 << 3) | 2;             // RelayToAppMessage.ble_connection_status_response
    pp = pb_put_varint(proto, pp, (uint32_t)ip);
    memcpy(proto + pp, inner, ip);
    pp += ip;
    return frame_wrap(proto, pp, out, out_cap);
}

// ---------------------------------------------------------------------------
// Encode device->host RelayToAppMessage{ clear_bonds_response{ success } } (field 5)
// ---------------------------------------------------------------------------
size_t mp_relay_encode_clear_bonds_response(bool success, uint8_t* out, size_t out_cap)
{
    uint8_t inner[4];
    size_t ip = 0;
    if (success) ip = pb_put_varint_field(inner, ip, 1, 1);   // success = true (false omitted)

    uint8_t proto[12];
    size_t pp = 0;
    proto[pp++] = (5 << 3) | 2;             // RelayToAppMessage.clear_bonds_response
    pp = pb_put_varint(proto, pp, (uint32_t)ip);
    memcpy(proto + pp, inner, ip);
    pp += ip;
    return frame_wrap(proto, pp, out, out_cap);
}

// ---------------------------------------------------------------------------
// Classify a host->device AppToRelayMessage. For PASSTHROUGH, sets *data/*dlen
// to the inner NUS payload. Dongle-level reads carry no payload.
// ---------------------------------------------------------------------------
static mp_relay_req_t classify_request(const uint8_t* proto, size_t proto_len,
                                       const uint8_t** data, uint16_t* dlen)
{
    size_t pos = 0;
    while (pos < proto_len) {
        uint32_t key;
        if (!pb_get_varint(proto, proto_len, &pos, &key)) return MP_RELAY_REQ_NONE;
        uint32_t field = key >> 3;
        uint32_t wt = key & 0x07;

        if (wt == 0) {                      // varint (e.g. destination) — skip
            uint32_t tmp;
            if (!pb_get_varint(proto, proto_len, &pos, &tmp)) return MP_RELAY_REQ_NONE;
        } else if (wt == 2) {               // length-delimited oneof field
            uint32_t l;
            if (!pb_get_varint(proto, proto_len, &pos, &l)) return MP_RELAY_REQ_NONE;
            if (pos + l > proto_len) return MP_RELAY_REQ_NONE;

            if (field == 4) return MP_RELAY_REQ_DEVICE_INFO_READ;
            if (field == 2) return MP_RELAY_REQ_CONN_STATUS_READ;
            if (field == 5) return MP_RELAY_REQ_CLEAR_BONDS;
            if (field == 3) {
                // pass_through_to_mouthpad submessage — find field 1 (data)
                const uint8_t* sub = proto + pos;
                size_t spos = 0;
                while (spos < l) {
                    uint32_t skey;
                    if (!pb_get_varint(sub, l, &spos, &skey)) break;
                    uint32_t sfield = skey >> 3;
                    uint32_t swt = skey & 0x07;
                    if (swt == 2) {
                        uint32_t sl;
                        if (!pb_get_varint(sub, l, &spos, &sl)) break;
                        if (spos + sl > l) break;
                        if (sfield == 1) {  // data
                            *data = sub + spos;
                            *dlen = (uint16_t)sl;
                            return MP_RELAY_REQ_PASSTHROUGH;
                        }
                        spos += sl;
                    } else if (swt == 0) {
                        uint32_t tmp;
                        if (!pb_get_varint(sub, l, &spos, &tmp)) break;
                    } else if (swt == 5) { spos += 4; }
                    else if (swt == 1) { spos += 8; }
                    else break;
                }
                return MP_RELAY_REQ_PASSTHROUGH;   // passthrough w/o data
            }
            pos += l;                       // other oneof (clear bonds/dfu/etc.)
            return MP_RELAY_REQ_OTHER;
        } else if (wt == 5) { pos += 4; }   // 32-bit
        else if (wt == 1) { pos += 8; }     // 64-bit
        else return MP_RELAY_REQ_NONE;
    }
    return MP_RELAY_REQ_NONE;
}

// ---------------------------------------------------------------------------
// Reconstructor
// ---------------------------------------------------------------------------
void mp_relay_rx_init(mp_relay_rx_t* rx, mp_relay_request_cb cb, void* ctx)
{
    rx->acc_len = 0;
    rx->cb = cb;
    rx->ctx = ctx;
}

// Drop the first `n` bytes from the accumulator (resync).
static void acc_drop(mp_relay_rx_t* rx, uint16_t n)
{
    if (n >= rx->acc_len) { rx->acc_len = 0; return; }
    memmove(rx->acc, rx->acc + n, rx->acc_len - n);
    rx->acc_len -= n;
}

void mp_relay_rx_feed(mp_relay_rx_t* rx, const uint8_t* data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (rx->acc_len < MP_RELAY_MAX_FRAME) {
            rx->acc[rx->acc_len++] = data[i];
        } else {
            // Overflow without a valid frame — drop a byte and keep scanning.
            acc_drop(rx, 1);
            rx->acc[rx->acc_len++] = data[i];
        }

        // Try to parse as many complete frames as are buffered.
        for (;;) {
            // Need at least the 4-byte header.
            if (rx->acc_len < 4) break;

            // Resync to magic 0xAA 0x55.
            if (rx->acc[0] != 0xAA || rx->acc[1] != 0x55) {
                // Find next 0xAA and drop everything before it.
                uint16_t j = 1;
                while (j < rx->acc_len && rx->acc[j] != 0xAA) j++;
                acc_drop(rx, j);
                if (rx->acc_len < 4) break;
                if (rx->acc[0] != 0xAA || rx->acc[1] != 0x55) {
                    // Lone 0xAA without 0x55 follower — drop it and retry.
                    if (rx->acc_len >= 2 && rx->acc[1] != 0x55) { acc_drop(rx, 1); }
                    continue;
                }
            }

            uint16_t plen = ((uint16_t)rx->acc[2] << 8) | rx->acc[3];
            if (plen > MP_RELAY_MAX_NUS_PAYLOAD + 16) {
                // Implausible length — bad header, resync past this magic.
                acc_drop(rx, 2);
                continue;
            }
            uint16_t frame_len = 4 + plen + 2;
            if (rx->acc_len < frame_len) break;   // wait for more bytes

            const uint8_t* proto = rx->acc + 4;
            uint16_t rx_crc = ((uint16_t)rx->acc[4 + plen] << 8) | rx->acc[4 + plen + 1];
            if (mp_relay_crc16(proto, plen) == rx_crc) {
                if (rx->cb) {
                    const uint8_t* d = NULL; uint16_t dl = 0;
                    mp_relay_req_t type = classify_request(proto, plen, &d, &dl);
                    if (type != MP_RELAY_REQ_NONE) rx->cb(type, d, dl, rx->ctx);
                }
                acc_drop(rx, frame_len);
            } else {
                // CRC fail — drop the magic and resync.
                acc_drop(rx, 2);
            }
        }
    }
}
