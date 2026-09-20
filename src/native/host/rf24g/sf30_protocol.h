// sf30_protocol.h - SF30 2.4G RF protocol constants
//
// Every value here was recovered from a logic-analyser tap on the SPI bus
// between the original 8BitDo receiver's MCU and its radio. They are
// ground truth read off real register writes, not inference -- do not
// "fix" them. Full capture notes and reverse-engineering detail:
// https://github.com/FatBeard/sf30-2.4g-protocol
//
// Deliberately free of pico-sdk / platform calls -- see rf24g_hal.h. Only
// rf24g_host.c includes this file, and it must include rf24g_hal.h first
// (see rf24g_host.c's include block) so __not_in_flash_func is defined
// before sf30_identity()/sf30_next_idx() below use it: both are called from
// rf24g_host.c's ISR/alarm chain (sf30_identity() via apply_pipe_addresses()
// <- pairing_end() <- on_alarm(); sf30_next_idx() from handle_frame()/
// on_radio_irq()/on_alarm()/enter_search()), and `static inline` alone is
// only ever a hint -- nothing guarantees the compiler actually inlines
// either one at every call site (a lower optimization level, e.g. debug
// builds, is enough to break that assumption), so both need the same
// RAM-residency guarantee as everything else reachable from that chain --
// see rf24g_host.c's file header.

#ifndef SF30_PROTOCOL_H
#define SF30_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>

// ---- Radio ---------------------------------------------------------- */

// RX_ADDR_P0 / TX_ADDR, in bus order (nRF24 multi-byte registers are
// LSByte-first). The OEM dongle's own link address -- unused by this driver
// (we always assign a receiver-owned identity, see "Receiver identity"
// below) but kept for a future OEM-address diagnostic mode.
static const uint8_t SF30_ADDR[5] = { 0xE4, 0x81, 0x00, 0xEA, 0xB3 };

// The pairing rendezvous address (see "Pairing rendezvous" below) -- pipe 1
// on the OEM dongle.
static const uint8_t SF30_ADDR_P1[5] = { 0xE4, 0x17, 0xD8, 0x55, 0xAA };

// Idle/unpaired park channel: RF_CH 0x2F = 2447 MHz. Deliberately chosen by
// the original firmware because it is itself a hop channel (indices 2, 29,
// 33 and 62), so a controller that is running the sequence is guaranteed to
// visit it.
#define SF30_PARK_CH 0x2F

// 16 channels over a 64-step sequence, exact period, no drift.
//   RF_CH = SF30_HOP_TABLE[index];  freq = 2400 + RF_CH MHz
static const uint8_t SF30_HOP_TABLE[64] = {
      6,  24,  47,  67,  11,  29,  51,  69,
     14,  33,  55,  73,  17,  36,  59,  76,
     76,  59,  36,  17,  73,  55,  33,  14,
     69,  51,  29,  11,  67,  47,  24,   6,
     67,  47,  24,   6,  69,  51,  29,  11,
     73,  55,  33,  14,  76,  59,  36,  17,
     17,  36,  59,  76,  14,  33,  55,  73,
     11,  29,  51,  69,   6,  24,  47,  67,
};

// ---- Receiver identity --------------------------------------------------
//
// Pairing hands the controller whatever address the receiver offers -- see
// "Pairing rendezvous" below. A replacement receiver can therefore be given
// an address of its own, which is the only way to tell two of them apart:
// on the OEM address they are indistinguishable, same pipe, same framing.
//
// The address's LSByte (0xE4) is fixed; the remaining four bytes are a
// receiver ID, PERMANENT and never persisted -- computed by
// sf30_derive_receiver_id() below from the board's own factory-programmed
// unique ID, so it comes out identical on every boot with nothing written
// to flash and nothing to lose on a power cycle. This mirrors the OEM
// dongle itself -- its address is burned in at the factory, permanent and
// unique per unit, and pairing writes it into the CONTROLLER's memory,
// never the receiver's. Reflashing the same board keeps the controller
// paired to it; moving to a different board changes the receiver ID (a
// different unique ID hashes differently) and requires re-pairing, exactly
// like swapping in a different physical OEM dongle would.
#define SF30_IDENT_LSB 0xE4

// Fallback receiver ID for a degenerate sf30_derive_receiver_id() result
// (see below). Not a "factory default" in the OEM sense -- every board
// computes its own permanent ID -- this is only reached on the rare hash
// collision with a reserved address (an all-equal result, or one that
// aliases SF30_ADDR_P1 or SF30_ADDR's own upper bytes).
static const uint8_t SF30_IDENT_BODY[4] = { 0x5A, 0x31, 0xC7, 0x92 };

// Builds the receiver's address under receiver ID `body`, in bus order,
// into 5 bytes.
//
// Called from apply_pipe_addresses() (rf24g_host.c), which is reachable
// from on_alarm() via pairing_end() during pairing, so this has to stay off
// any flash-resident libc call. Four EXPLICIT scalar stores, not a loop: a
// `for` loop here, even with a compile-time-constant trip count, is fair
// game for GCC's tree-loop-distribute-patterns pass, which can turn it back
// into a runtime memcpy() call -- confirmed by disassembly to happen on the
// RP2040 (Cortex-M0+) build even though the equivalent loop built clean on
// RP2350. Do not "clean this up" into a loop or memcpy call without
// re-checking disassembly on both targets.
static inline void __not_in_flash_func(sf30_identity)(const uint8_t *body, uint8_t *out)
{
    out[0] = SF30_IDENT_LSB;
    out[1] = body[0];
    out[2] = body[1];
    out[3] = body[2];
    out[4] = body[3];
}

// FNV-1a over the whole board uid, into a 4-byte receiver ID.
//
// Hashing all `len` bytes rather than truncating to 4 matters: RP2040
// boards from one production reel commonly share a flash-ID prefix (the
// vendor/lot fields are the high-order bytes), so a naive truncation could
// hand two boards off the same reel the same receiver ID. FNV-1a mixes
// every input byte into every output byte, so a shared prefix alone isn't
// enough to collide.
//
// Three results are degenerate and get remapped to SF30_IDENT_BODY instead
// of being used as-is:
//   - all four output bytes equal (covers 00 00 00 00 and FF FF FF FF --
//     what you get if platform_get_unique_id() ever reads back a flash
//     chip that isn't answering the ID command)
//   - 17 D8 55 AA -- SF30_ADDR_P1's own upper bytes; the receiver's link
//     address would alias the pairing rendezvous address itself
//   - 81 00 EA B3 -- SF30_ADDR's own upper bytes, the OEM dongle's body
// Both aliasing cases are pathological (any real 64-bit flash ID landing
// on either exact value is astronomically unlikely), but they're cheap to
// guard against and the failure mode -- a receiver indistinguishable from
// the pairing channel or the dongle it's replacing -- is bad enough that
// it's worth being paranoid here.
static inline void sf30_derive_receiver_id(const uint8_t *uid, size_t len, uint8_t *out4)
{
    uint32_t hash = 0x811c9dc5u;   // FNV-1a 32-bit offset basis
    for (size_t i = 0; i < len; i++) {
        hash ^= uid[i];
        hash *= 0x01000193u;       // FNV-1a 32-bit prime
    }
    out4[0] = (uint8_t)(hash >> 24);
    out4[1] = (uint8_t)(hash >> 16);
    out4[2] = (uint8_t)(hash >> 8);
    out4[3] = (uint8_t)(hash);

    bool all_equal = (out4[0] == out4[1]) && (out4[1] == out4[2]) && (out4[2] == out4[3]);
    bool aliases_p1  = memcmp(out4, SF30_ADDR_P1 + 1, 4) == 0;
    bool aliases_oem = memcmp(out4, SF30_ADDR + 1, 4) == 0;
    if (all_equal || aliases_p1 || aliases_oem) {
        memcpy(out4, SF30_IDENT_BODY, 4);
    }
}

// ---- Pairing rendezvous -------------------------------------------------
//
// Request   (controller -> receiver, 4 bytes):  35 04 00 <counter8>
// Response  (receiver -> controller, 13 bytes): 35 0D 01 <addr> <~addr>
//
// Bytes 8-12 are the one's complement of the offered address -- the captured
// pair E4 81 00 EA B3 / 1B 7E FF 15 4C XORs to FF on all five bytes. They are
// a check field bound to the address, not a receiver ID, so an offer of a
// DIFFERENT address must recompute them. Copying the dongle's tail verbatim
// alongside a different address makes every reply invalid by construction.
//
// The rendezvous address is SF30_ADDR_P1 and the channel is SF30_PARK_CH;
// RF_CH is never written for the duration of the exchange.
#define SF30_PAIR_HDR      0x35
#define SF30_PAIR_REQ      0x00   /* byte 2: message type, request  */
#define SF30_PAIR_RSP      0x01   /* byte 2: message type, response */
#define SF30_PAIR_REQ_LEN  4
#define SF30_PAIR_RSP_LEN  13

static inline void sf30_pair_reply(const uint8_t *addr, uint8_t *out13)
{
    out13[0] = SF30_PAIR_HDR;
    out13[1] = SF30_PAIR_RSP_LEN;
    out13[2] = SF30_PAIR_RSP;
    for (uint8_t i = 0; i < 5; i++) {
        out13[3 + i] = addr[i];
        out13[8 + i] = addr[i] ^ 0xFF;
    }
}

// ---- Packet framing -------------------------------------------------
//
//   80 0D <b2> <b3> 00 00 80 80 80 80 <idx> <cyc> 10   <- at rest
//    0  1   2    3   4  5  6  7  8  9   10    11   12
#define SF30_PKT_LEN      13
#define SF30_OFF_HEADER    0   /* 0x80 constant */
#define SF30_OFF_LENGTH    1   /* 0x0D constant (13) */
#define SF30_OFF_BTN_LO    2   /* d-pad + face buttons */
#define SF30_OFF_BTN_HI    3   /* L/R/Select/Start + Up's second bit */
#define SF30_OFF_COUNTER  10   /* hop index, 0x00-0x3F -- NOT a plain counter */
#define SF30_OFF_CYCLE    11   /* <base> | (counter >> 4), base is NOT constant */
#define SF30_OFF_TRAILER  12   /* 0x10 constant, but 0x31 on the contact frame */

#define SF30_CYCLE_MASK   0x03 /* the only bits of byte 11 that are understood */

#define SF30_HDR_MAGIC  0x80
#define SF30_LEN_MAGIC  0x0D
#define SF30_TRL_MAGIC  0x10

// ---- The frame counter, and why byte 10 is not it -----------------------
//
// Byte 10 does NOT run 0->63 and wrap. It runs in 16-frame blocks, and the
// blocks come in a fixed non-ascending order:
//
//     32..47, 48..63, 16..31, 0..15, 32..47, ...
//
// so there are exactly three non-`+1` transitions in every 64 frames:
// 63->16, 15->32 and 31->0.
//
// The true frame counter is monotonic and lives across both bytes:
//
//     counter = ((byte11 & 3) << 4) | (byte10 & 0x0F)
//
// which steps by exactly +1 on nearly all adjacent pairs (the rare
// exception a +2 where the source capture itself dropped a frame). So byte
// 11's low two bits are the counter's HIGH two bits, and byte 10 is the same
// counter with its high two bits permuted -- that permutation being the hop
// index.
//
// Consequence: `index + 1` is the wrong successor three times in every 64
// hops, and because both sides then advance by one the error PERSISTS
// rather than self-correcting -- measured a >1.5x throughput hit on real
// hardware. Use sf30_next_idx().
//
// The upper six bits of byte 11 are a separate open question. Do not
// decode them.

// Block order 2 -> 3 -> 1 -> 0 -> 2, indexed by the current block.
static inline uint8_t __not_in_flash_func(sf30_next_idx)(uint8_t idx)
{
    static const uint8_t NEXT_BLK[4] = { 2, 0, 3, 1 };
    idx &= 0x3F;
    return ((idx & 0x0F) != 0x0F) ? (uint8_t)(idx + 1)
                                  : (uint8_t)(NEXT_BLK[idx >> 4] << 4);
}

// ---- Button bits ----------------------------------------------------- */
// Combinations are a plain bitwise OR -- no interlocks, no SOCD handling, no
// special diagonal encoding. Bytes 4-5 are unused; 6-9 are constant 0x80.

// byte 2
#define SF30_UP     0x80
#define SF30_DOWN   0x40
#define SF30_LEFT   0x20
#define SF30_RIGHT  0x10
#define SF30_A      0x08
#define SF30_B      0x04
#define SF30_X      0x02
#define SF30_Y      0x01

// byte 3
#define SF30_UP2    0x40   /* set together with SF30_UP -- see quirk below */
#define SF30_START  0x20
#define SF30_SELECT 0x10
#define SF30_R      0x08
#define SF30_L      0x01

// Quirk: pressing Up sets byte2:0x80 AND byte3:0x40 together. byte3:0x40 has
// never been observed without byte2:0x80, and stayed clear through both the
// A/B/X/Y and L/R/Select/Start rotations, so it is not a mis-attributed
// shoulder or Select/Start bit. Deterministic but unexplained. Treat either
// bit as Up; never surface SF30_UP2 as a button of its own.
static inline bool sf30_up_pressed(uint8_t b2, uint8_t b3)
{
    return (b2 & SF30_UP) || (b3 & SF30_UP2);
}

// Bits 1, 2 and 7 of byte 3 were never observed set.

// byte3:0x20 (Start) is ALSO the controller's power button, so it is set on
// most unlinked probe frames (power-on/power-off both hold it). A decoder
// must gate button decoding on the link being established -- see
// rf24g_host.c's `probe` handling -- or it reports a phantom Start press
// every time an unlinked controller probes.

#endif /* SF30_PROTOCOL_H */
