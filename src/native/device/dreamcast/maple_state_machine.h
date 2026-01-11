// maple_state_machine.h - Maple Bus RX State Machine
// Ported from MaplePad by Charlie Cole / mackieks

#ifndef MAPLE_STATE_MACHINE_H
#define MAPLE_STATE_MACHINE_H

#include <stdint.h>

#define MAPLE_NUM_STATES 40
#define MAPLE_NUM_SETBITS 64

typedef struct {
    uint16_t NewState : 6;
    uint16_t Push : 1;
    uint16_t Error : 1;
    uint16_t Reset : 1;
    uint16_t End : 1;
    uint16_t SetBitsIndex : 6;
} MapleStateMachine;

// The state machine table - pre-calculated responses for any byte from Maple RX PIO
extern MapleStateMachine MapleMachine[MAPLE_NUM_STATES][256];  // 20KB

// Bits to set indexed from StateMachine::SetBitsIndex
extern uint8_t MapleSetBits[MAPLE_NUM_SETBITS][2];  // 128 bytes

// Build the state machine tables (call once at init)
void maple_build_state_machine_tables(void);

#endif // MAPLE_STATE_MACHINE_H
