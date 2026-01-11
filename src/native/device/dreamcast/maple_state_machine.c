// maple_state_machine.c - Maple Bus RX State Machine
// Ported from MaplePad by Charlie Cole / mackieks
//
// This implements a pre-calculated state machine for decoding Maple Bus packets.
// The RX PIO pushes 4 transitions (8 bits) at a time, and we use a 40-state
// machine to decode them into packet bytes.

#include "maple_state_machine.h"
#include <string.h>
#include <assert.h>

// Simple state machine states
enum {
    STATUS_NONE = -1,
    STATUS_START,
    STATUS_END,
    STATUS_PUSHBIT0,
    STATUS_PUSHBIT1,
    STATUS_PUSHBIT2,
    STATUS_PUSHBIT3,
    STATUS_PUSHBIT4,
    STATUS_PUSHBIT5,
    STATUS_PUSHBIT6,
    STATUS_PUSHBIT7,
    STATUS_BITSET = 0x80,
};

typedef struct {
    int Next[4];
    int Status;
} SimpleState;

static SimpleState States[MAPLE_NUM_STATES];
static int NumStates = 0;

// Combined state machine tables
MapleStateMachine MapleMachine[MAPLE_NUM_STATES][256];
uint8_t MapleSetBits[MAPLE_NUM_SETBITS][2];
static int SetBitsEntries = 0;

static int NewState(int Expected)
{
    int New = NumStates++;
    SimpleState *s = &States[New];
    memset(s, 0xFF, sizeof(*s));
    s->Next[Expected] = New;
    return New;
}

static int ExpectState(int ParentState, int Expected)
{
    int New = NewState(Expected);
    States[ParentState].Next[Expected] = New;
    return New;
}

static int ExpectStateWithStatus(int ParentState, int Expected, int Status)
{
    int New = NewState(Expected);
    States[New].Status = Status;
    States[ParentState].Next[Expected] = New;
    return New;
}

static int ExpectStateTwoParents(int ParentState, int OtherParentState, int Expected)
{
    int New = NewState(Expected);
    States[ParentState].Next[Expected] = New;
    States[OtherParentState].Next[Expected] = New;
    return New;
}

static void BuildBasicStates(void)
{
    // The transitions we expect for a valid Maple Bus stream
    // 0b10 is Maple bus pin 5 high
    // 0b01 is Maple bus pin 1 high
    // Reference: http://mc.pp.se/dc/maplewire.html

    // Start sequence (11 states)
    int Prev = NewState(0b11);
    Prev = ExpectState(Prev, 0b10);
    Prev = ExpectState(Prev, 0b00);
    Prev = ExpectState(Prev, 0b10);
    Prev = ExpectState(Prev, 0b00);
    Prev = ExpectState(Prev, 0b10);
    Prev = ExpectState(Prev, 0b00);
    Prev = ExpectState(Prev, 0b10);
    Prev = ExpectState(Prev, 0b00);
    Prev = ExpectState(Prev, 0b10);
    Prev = ExpectStateWithStatus(Prev, 0b11, STATUS_START);

    // Data bytes (6*4 = 24 states)
    // Each bit encoded separately so no shifting needed when receiving
    int PossibleEnd;
    int Option = Prev;
    int StartByte = NumStates;
    for (int i = 0; i < 4; i++) {
        Prev = ExpectStateTwoParents(Option, Prev, 0b01);
        Option = ExpectStateWithStatus(Prev, 0b11, (STATUS_PUSHBIT0 + i * 2) | STATUS_BITSET);
        Prev = ExpectStateWithStatus(Prev, 0b00, (STATUS_PUSHBIT0 + i * 2));
        if (i == 0) {
            PossibleEnd = Option;
        }

        Prev = ExpectStateTwoParents(Option, Prev, 0b10);
        Option = ExpectStateWithStatus(Prev, 0b11, (STATUS_PUSHBIT1 + i * 2) | STATUS_BITSET);
        Prev = ExpectStateWithStatus(Prev, 0b00, (STATUS_PUSHBIT1 + i * 2));

        if (i == 3) {
            // Loop back for next byte
            States[Option].Next[0b01] = StartByte;
            States[Prev].Next[0b01] = StartByte;
        }
    }

    // End sequence (5 states)
    Prev = ExpectState(PossibleEnd, 0b01);
    Prev = ExpectState(Prev, 0b00);
    // Signal end now - need to be at least 4 transitions back from real end
    // as PIO only pushes a byte (4 transitions) at a time
    Prev = ExpectStateWithStatus(Prev, 0b01, STATUS_END);
    Prev = ExpectState(Prev, 0b00);
    Prev = ExpectState(Prev, 0b01);
    States[Prev].Next[0b11] = 0;

    assert(NumStates == MAPLE_NUM_STATES);
}

static int FindOrAddSetBits(uint8_t CurrentByte, uint8_t NextByte)
{
    for (int i = 0; i < SetBitsEntries; i++) {
        if (MapleSetBits[i][0] == CurrentByte && MapleSetBits[i][1] == NextByte) {
            return i;
        }
    }
    int NewEntry = SetBitsEntries++;
    assert(NewEntry < MAPLE_NUM_SETBITS);
    MapleSetBits[NewEntry][0] = CurrentByte;
    MapleSetBits[NewEntry][1] = NextByte;
    return NewEntry;
}

void maple_build_state_machine_tables(void)
{
    BuildBasicStates();

    // Pre-calculate response for every possible byte in every starting state
    for (int StartingState = 0; StartingState < MAPLE_NUM_STATES; StartingState++) {
        for (int ByteFromPIO = 0; ByteFromPIO < 256; ByteFromPIO++) {
            MapleStateMachine M = {0};
            int State = StartingState;
            int Transitions = ByteFromPIO;
            int LastState = State;
            uint8_t DataBytes[2] = {0, 0};
            int CurrentDataByte = 0;

            for (int i = 0; i < 4; i++) {
                State = States[State].Next[(Transitions >> 6) & 3];
                if (State < 0) {
                    M.Error = 1;
                    State = 0;
                }

                if (State != LastState) {
                    int Status = States[State].Status;
                    if (Status & STATUS_BITSET) {
                        // Data received most significant bit first
                        DataBytes[CurrentDataByte] |= (1 << (7 - ((Status & ~STATUS_BITSET) - STATUS_PUSHBIT0)));
                    }
                    switch (States[State].Status & ~STATUS_BITSET) {
                    case STATUS_START:
                        M.Reset = 1;
                        break;
                    case STATUS_END:
                        M.End = 1;
                        break;
                    case STATUS_PUSHBIT7:  // Last bit of current byte
                        M.Push = 1;
                        CurrentDataByte = 1;
                        break;
                    }
                    LastState = State;
                }
                Transitions <<= 2;
            }

            M.NewState = State;
            M.SetBitsIndex = FindOrAddSetBits(DataBytes[0], DataBytes[1]);
            MapleMachine[StartingState][ByteFromPIO] = M;
        }
    }
}
