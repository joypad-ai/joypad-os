// dreamcast_device.c - Dreamcast Maple Bus output interface
// Emulates a Dreamcast controller using PIO for precise timing
//
// Ported from MaplePad by Charlie Cole / mackieks
// https://github.com/mackieks/MaplePad
//
// Architecture:
// - Core 1: RX only - decodes Maple Bus packets into ring buffer
// - Core 0: Processes packets and sends responses via DMA

#include "dreamcast_device.h"
#include "maple_state_machine.h"
#include "maple.pio.h"
#include "core/output_interface.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/uart.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// PIO AND DMA CONFIGURATION
// ============================================================================

// TX on pio0 (shared with WS2812 on different GPIO), RX on pio1
#define TXPIO pio0
#define RXPIO pio1

static uint tx_dma_channel = 0;
static uint tx_sm = 0;

// ============================================================================
// MAPLE BUS ADDRESSING
// ============================================================================

#define ADDRESS_DREAMCAST       0x00
#define ADDRESS_CONTROLLER      0x20
#define ADDRESS_SUBPERIPHERAL0  0x01
#define ADDRESS_SUBPERIPHERAL1  0x02
#define ADDRESS_PORT_MASK       0xC0
#define ADDRESS_PERIPHERAL_MASK 0x3F

// ============================================================================
// MAPLE BUS COMMANDS
// ============================================================================

enum {
    CMD_RESPOND_FILE_ERROR = -5,
    CMD_RESPOND_SEND_AGAIN = -4,
    CMD_RESPOND_UNKNOWN_COMMAND = -3,
    CMD_RESPOND_FUNC_CODE_UNSUPPORTED = -2,
    CMD_NO_RESPONSE = -1,
    CMD_DEVICE_REQUEST = 1,
    CMD_ALL_STATUS_REQUEST,
    CMD_RESET_DEVICE,
    CMD_SHUTDOWN_DEVICE,
    CMD_RESPOND_DEVICE_STATUS,
    CMD_RESPOND_ALL_DEVICE_STATUS,
    CMD_RESPOND_COMMAND_ACK,
    CMD_RESPOND_DATA_TRANSFER,
    CMD_GET_CONDITION,
    CMD_GET_MEDIA_INFO,
    CMD_BLOCK_READ,
    CMD_BLOCK_WRITE,
    CMD_BLOCK_COMPLETE_WRITE,
    CMD_SET_CONDITION
};

enum {
    FUNC_CONTROLLER = 1,
    FUNC_MEMORY_CARD = 2,
    FUNC_LCD = 4,
    FUNC_TIMER = 8,
    FUNC_VIBRATION = 256
};

// ============================================================================
// PACKET STRUCTURES (match MaplePad format)
// ============================================================================

typedef struct __attribute__((packed)) {
    int8_t Command;
    uint8_t Destination;
    uint8_t Origin;
    uint8_t NumWords;
} PacketHeader;

typedef struct __attribute__((packed)) {
    uint32_t Func;
    uint32_t FuncData[3];
    int8_t AreaCode;
    uint8_t ConnectorDirection;
    char ProductName[30];
    char ProductLicense[60];
    uint16_t StandbyPower;
    uint16_t MaxPower;
} PacketDeviceInfo;

typedef struct __attribute__((packed)) {
    uint32_t Condition;
    uint16_t Buttons;
    uint8_t RightTrigger;
    uint8_t LeftTrigger;
    uint8_t JoyX;
    uint8_t JoyY;
    uint8_t JoyX2;
    uint8_t JoyY2;
} PacketControllerCondition;

// Pre-built packet types with BitPairsMinus1 prefix for DMA
typedef struct __attribute__((packed)) {
    uint32_t BitPairsMinus1;
    PacketHeader Header;
    PacketDeviceInfo Info;
    uint32_t CRC;
} FInfoPacket;

typedef struct __attribute__((packed)) {
    uint32_t BitPairsMinus1;
    PacketHeader Header;
    PacketControllerCondition Controller;
    uint32_t CRC;
} FControllerPacket;

typedef struct __attribute__((packed)) {
    uint32_t BitPairsMinus1;
    PacketHeader Header;
    uint32_t CRC;
} FACKPacket;

// ============================================================================
// BUFFERS
// ============================================================================

#define RX_BUFFER_SIZE 4096

static uint8_t RxBuffer[RX_BUFFER_SIZE] __attribute__((aligned(4)));
static uint8_t Packet[1024 + 8] __attribute__((aligned(4)));

// Pre-built response packets
static FInfoPacket InfoPacket;
static FControllerPacket ControllerPacket;
static FACKPacket ACKPacket;

// ============================================================================
// CONTROLLER STATE
// ============================================================================

static dc_controller_state_t dc_state[MAX_PLAYERS];
static uint8_t dc_rumble[MAX_PLAYERS];

// ============================================================================
// SEND STATE
// ============================================================================

typedef enum {
    SEND_NOTHING,
    SEND_CONTROLLER_INFO,
    SEND_CONTROLLER_STATUS,
    SEND_ACK,
} ESendState;

static volatile ESendState NextPacketSend = SEND_NOTHING;

// ============================================================================
// CRC CALCULATION
// ============================================================================

static uint32_t CalcCRC(const uint32_t *Words, uint32_t NumWords)
{
    uint32_t XOR = 0;
    for (uint32_t i = 0; i < NumWords; i++) {
        XOR ^= Words[i];
    }
    XOR ^= (XOR << 16);
    XOR ^= (XOR << 8);
    return XOR;
}

// ============================================================================
// PACKET BUILDERS
// ============================================================================

static void BuildInfoPacket(void)
{
    InfoPacket.BitPairsMinus1 = (sizeof(InfoPacket) - 7) * 4 - 1;

    InfoPacket.Header.Command = CMD_RESPOND_DEVICE_STATUS;
    InfoPacket.Header.Destination = ADDRESS_DREAMCAST;
    InfoPacket.Header.Origin = ADDRESS_CONTROLLER;
    InfoPacket.Header.NumWords = sizeof(InfoPacket.Info) / sizeof(uint32_t);

    InfoPacket.Info.Func = __builtin_bswap32(FUNC_CONTROLLER);
    InfoPacket.Info.FuncData[0] = __builtin_bswap32(0x000f06fe);  // Buttons supported
    InfoPacket.Info.FuncData[1] = 0;
    InfoPacket.Info.FuncData[2] = 0;
    InfoPacket.Info.AreaCode = -1;  // All regions
    InfoPacket.Info.ConnectorDirection = 0;
    strncpy(InfoPacket.Info.ProductName, "Dreamcast Controller          ", sizeof(InfoPacket.Info.ProductName));
    strncpy(InfoPacket.Info.ProductLicense,
            "Produced By or Under License From SEGA ENTERPRISES,LTD.     ",
            sizeof(InfoPacket.Info.ProductLicense));
    InfoPacket.Info.StandbyPower = 430;
    InfoPacket.Info.MaxPower = 500;

    InfoPacket.CRC = CalcCRC((uint32_t *)&InfoPacket.Header, sizeof(InfoPacket) / sizeof(uint32_t) - 2);
}

static void BuildControllerPacket(void)
{
    ControllerPacket.BitPairsMinus1 = (sizeof(ControllerPacket) - 7) * 4 - 1;

    ControllerPacket.Header.Command = CMD_RESPOND_DATA_TRANSFER;
    ControllerPacket.Header.Destination = ADDRESS_DREAMCAST;
    ControllerPacket.Header.Origin = ADDRESS_CONTROLLER;
    ControllerPacket.Header.NumWords = sizeof(ControllerPacket.Controller) / sizeof(uint32_t);

    ControllerPacket.Controller.Condition = __builtin_bswap32(FUNC_CONTROLLER);
    ControllerPacket.Controller.Buttons = 0xFFFF;  // All released
    ControllerPacket.Controller.RightTrigger = 0;
    ControllerPacket.Controller.LeftTrigger = 0;
    ControllerPacket.Controller.JoyX = 128;
    ControllerPacket.Controller.JoyY = 128;
    ControllerPacket.Controller.JoyX2 = 128;
    ControllerPacket.Controller.JoyY2 = 128;

    ControllerPacket.CRC = CalcCRC((uint32_t *)&ControllerPacket.Header, sizeof(ControllerPacket) / sizeof(uint32_t) - 2);
}

static void BuildACKPacket(void)
{
    ACKPacket.BitPairsMinus1 = (sizeof(ACKPacket) - 7) * 4 - 1;

    ACKPacket.Header.Command = CMD_RESPOND_COMMAND_ACK;
    ACKPacket.Header.Destination = ADDRESS_DREAMCAST;
    ACKPacket.Header.Origin = ADDRESS_CONTROLLER;
    ACKPacket.Header.NumWords = 0;

    ACKPacket.CRC = CalcCRC((uint32_t *)&ACKPacket.Header, sizeof(ACKPacket) / sizeof(uint32_t) - 2);
}

// ============================================================================
// PACKET SENDING
// ============================================================================

static void SendPacket(const uint32_t *Words, uint32_t NumWords)
{
    // Fix port number in header (doesn't change CRC as same on Origin and Destination)
    PacketHeader *Header = (PacketHeader *)(Words + 1);
    Header->Origin = (Header->Origin & ADDRESS_PERIPHERAL_MASK) | (((PacketHeader *)Packet)->Origin & ADDRESS_PORT_MASK);
    Header->Destination = (Header->Destination & ADDRESS_PERIPHERAL_MASK) | (((PacketHeader *)Packet)->Origin & ADDRESS_PORT_MASK);

    dma_channel_set_read_addr(tx_dma_channel, Words, false);
    dma_channel_set_trans_count(tx_dma_channel, NumWords, true);
}

static void SendControllerStatus(void)
{
    // Update controller state from our state
    ControllerPacket.Controller.Buttons = dc_state[0].buttons;
    ControllerPacket.Controller.RightTrigger = dc_state[0].rt;
    ControllerPacket.Controller.LeftTrigger = dc_state[0].lt;
    ControllerPacket.Controller.JoyX = dc_state[0].joy_x;
    ControllerPacket.Controller.JoyY = dc_state[0].joy_y;
    ControllerPacket.Controller.JoyX2 = dc_state[0].joy2_x;
    ControllerPacket.Controller.JoyY2 = dc_state[0].joy2_y;

    // Recalculate CRC
    ControllerPacket.CRC = CalcCRC((uint32_t *)&ControllerPacket.Header, sizeof(ControllerPacket) / sizeof(uint32_t) - 2);

    SendPacket((uint32_t *)&ControllerPacket, sizeof(ControllerPacket) / sizeof(uint32_t));
}

// ============================================================================
// PACKET PROCESSING
// ============================================================================

// Debug counters
static volatile uint32_t cmd_device_req = 0;
static volatile uint32_t cmd_get_cond = 0;

static bool ConsumePacket(uint32_t Size)
{
    if ((Size & 3) != 1) {  // Should be even words + CRC byte
        return false;
    }

    Size--;  // Drop CRC byte
    if (Size == 0) {
        return false;
    }

    PacketHeader *Header = (PacketHeader *)Packet;
    uint32_t *PacketData = (uint32_t *)(Header + 1);

    if (Size != (Header->NumWords + 1) * 4) {
        return false;
    }

    // Mask off port number
    uint8_t DestPeripheral = Header->Destination & ADDRESS_PERIPHERAL_MASK;

    // Only respond to main controller requests
    if (DestPeripheral != ADDRESS_CONTROLLER) {
        return false;
    }

    switch (Header->Command) {
    case CMD_RESET_DEVICE:
        NextPacketSend = SEND_ACK;
        break;

    case CMD_DEVICE_REQUEST:
        cmd_device_req++;
        NextPacketSend = SEND_CONTROLLER_INFO;
        break;

    case CMD_GET_CONDITION:
        cmd_get_cond++;
        if (Header->NumWords >= 1) {
            uint32_t Func = __builtin_bswap32(PacketData[0]);
            if (Func == FUNC_CONTROLLER) {
                NextPacketSend = SEND_CONTROLLER_STATUS;
            }
        }
        break;

    case CMD_SET_CONDITION:
        if (Header->NumWords >= 2) {
            uint32_t Func = __builtin_bswap32(PacketData[0]);
            if (Func == FUNC_VIBRATION) {
                // Extract rumble data
                uint8_t *CondData = (uint8_t *)&PacketData[1];
                dc_rumble[0] = CondData[1];  // Power byte
            }
        }
        NextPacketSend = SEND_ACK;
        break;

    default:
        break;
    }

    return true;
}

// ============================================================================
// BUTTON MAPPING
// ============================================================================

static uint16_t map_buttons_to_dc(uint32_t jp_buttons)
{
    uint16_t dc_buttons = 0;

    if (jp_buttons & JP_BUTTON_B1) dc_buttons |= DC_MAP_B1;
    if (jp_buttons & JP_BUTTON_B2) dc_buttons |= DC_MAP_B2;
    if (jp_buttons & JP_BUTTON_B3) dc_buttons |= DC_MAP_B3;
    if (jp_buttons & JP_BUTTON_B4) dc_buttons |= DC_MAP_B4;
    if (jp_buttons & JP_BUTTON_L1) dc_buttons |= DC_MAP_L1;
    if (jp_buttons & JP_BUTTON_R1) dc_buttons |= DC_MAP_R1;
    if (jp_buttons & JP_BUTTON_S1) dc_buttons |= DC_MAP_S1;
    if (jp_buttons & JP_BUTTON_S2) dc_buttons |= DC_MAP_S2;
    if (jp_buttons & JP_BUTTON_DU) dc_buttons |= DC_MAP_DU;
    if (jp_buttons & JP_BUTTON_DD) dc_buttons |= DC_MAP_DD;
    if (jp_buttons & JP_BUTTON_DL) dc_buttons |= DC_MAP_DL;
    if (jp_buttons & JP_BUTTON_DR) dc_buttons |= DC_MAP_DR;
    if (jp_buttons & JP_BUTTON_A1) dc_buttons |= DC_MAP_A1;

    // Dreamcast uses active-low (0 = pressed)
    return ~dc_buttons;
}

// ============================================================================
// OUTPUT UPDATE
// ============================================================================

void __not_in_flash_func(dreamcast_update_output)(void)
{
    // Only update state if there's new input - router clears updated flag after read
    // so we must not call this too frequently or we'll miss updates
    for (int port = 0; port < MAX_PLAYERS; port++) {
        const input_event_t *event = router_get_output(OUTPUT_TARGET_DREAMCAST, port);
        if (!event || event->type == INPUT_TYPE_NONE) {
            // No new update - keep existing state (don't reset to defaults!)
            continue;
        }

        // New input available - update state
        dc_state[port].buttons = map_buttons_to_dc(event->buttons);
        dc_state[port].joy_x = event->analog[ANALOG_LX];
        dc_state[port].joy_y = event->analog[ANALOG_LY];
        dc_state[port].joy2_x = event->analog[ANALOG_RX];
        dc_state[port].joy2_y = event->analog[ANALOG_RY];
        dc_state[port].lt = event->analog[ANALOG_L2];
        dc_state[port].rt = event->analog[ANALOG_R2];
    }
}

// ============================================================================
// CORE 1: RX ONLY (must be in RAM for speed)
// ============================================================================

// Debug counters (volatile for Core0 to read)
static volatile uint32_t rx_bytes_count = 0;
static volatile uint32_t rx_resets_count = 0;
static volatile uint32_t rx_ends_count = 0;
static volatile uint32_t rx_errors_count = 0;
static volatile uint32_t rx_crc_fails = 0;
static volatile uint32_t rx_crc_ok = 0;
static volatile uint32_t core1_state = 0;  // 0=not started, 1=building, 2=ready, 3=running

// Handshake flags (can't use FIFO - it's used by flash_safe_execute lockout)
static volatile bool core1_ready = false;
static volatile bool core0_started_pio = false;

// Packet notification (can't use FIFO - use ring buffer with volatile indices)
static volatile uint32_t packet_end_write = 0;  // Written by Core1
static volatile uint32_t packet_end_read = 0;   // Read by Core0
static volatile uint32_t packet_ends[16];       // Ring buffer of packet end offsets

static void __no_inline_not_in_flash_func(core1_rx_task)(void)
{
    uint32_t State = 0;
    uint8_t Byte = 0;
    uint8_t XOR = 0;
    uint32_t StartOfPacket = 0;
    uint32_t Offset = 0;

    core1_state = 1;  // Building tables

    // Build state machine tables
    maple_build_state_machine_tables();

    core1_state = 2;  // Ready, waiting for Core0

    // Signal ready to core0 (use flag instead of FIFO - FIFO is used by flash lockout)
    core1_ready = true;
    __sev();  // Wake Core0 if waiting

    // Wait for core0 to start RX PIO
    while (!core0_started_pio) {
        __wfe();
    }

    // Flush RX FIFO
    while ((RXPIO->fstat & (1u << PIO_FSTAT_RXEMPTY_LSB)) == 0) {
        pio_sm_get(RXPIO, 0);
    }

    core1_state = 3;  // In RX loop

    while (true) {
        // Wait for data from RX PIO
        while ((RXPIO->fstat & (1u << PIO_FSTAT_RXEMPTY_LSB)) != 0)
            ;

        const uint8_t Value = RXPIO->rxf[0];
        rx_bytes_count++;

        MapleStateMachine M = MapleMachine[State][Value];
        State = M.NewState;

        if (M.Error) {
            rx_errors_count++;
        }

        if (M.Reset) {
            rx_resets_count++;
            Offset = StartOfPacket;
            Byte = 0;
            XOR = 0;
        }

        Byte |= MapleSetBits[M.SetBitsIndex][0];

        if (M.Push) {
            RxBuffer[Offset & (RX_BUFFER_SIZE - 1)] = Byte;
            XOR ^= Byte;
            Byte = MapleSetBits[M.SetBitsIndex][1];
            Offset++;
        }

        if (M.End) {
            rx_ends_count++;
            if (XOR == 0) {  // CRC valid
                rx_crc_ok++;
                // Write to ring buffer instead of FIFO (FIFO used by flash lockout)
                uint32_t next_write = (packet_end_write + 1) & 15;
                if (next_write != packet_end_read) {  // Not full
                    packet_ends[packet_end_write] = Offset;
                    packet_end_write = next_write;
                    __sev();  // Wake Core0
                    StartOfPacket = ((Offset + 3) & ~3);  // Align for swizzling
                }
            } else {
                rx_crc_fails++;
            }
        }
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

static void SetupMapleTX(void)
{
    tx_sm = pio_claim_unused_sm(TXPIO, true);
    uint offset = pio_add_program(TXPIO, &maple_tx_program);

    // Clock divider of 3.0 (from MaplePad)
    maple_tx_program_init(TXPIO, tx_sm, offset, MAPLE_PIN1, MAPLE_PIN5, 3.0f);

    // Setup DMA
    tx_dma_channel = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(tx_dma_channel);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_dreq(&cfg, pio_get_dreq(TXPIO, tx_sm, true));
    dma_channel_configure(
        tx_dma_channel,
        &cfg,
        &TXPIO->txf[tx_sm],
        NULL,
        0,
        false
    );

    gpio_pull_up(MAPLE_PIN1);
    gpio_pull_up(MAPLE_PIN5);
}

static void SetupMapleRX(void)
{
    uint offsets[3];
    offsets[0] = pio_add_program(RXPIO, &maple_rx_triple1_program);
    offsets[1] = pio_add_program(RXPIO, &maple_rx_triple2_program);
    offsets[2] = pio_add_program(RXPIO, &maple_rx_triple3_program);

    // Clock divider of 3.0 (from MaplePad)
    maple_rx_triple_program_init(RXPIO, offsets, MAPLE_PIN1, MAPLE_PIN5, 3.0f);

    // Print GPIO state for debugging
    printf("[DC] GPIO %d state: %d, GPIO %d state: %d\n",
           MAPLE_PIN1, gpio_get(MAPLE_PIN1),
           MAPLE_PIN5, gpio_get(MAPLE_PIN5));

    // Wait for core1 to be ready (use flag instead of FIFO - FIFO is used by flash lockout)
    while (!core1_ready) {
        __wfe();
    }

    // Enable RX state machines (order matters per MaplePad)
    pio_sm_set_enabled(RXPIO, 1, true);
    pio_sm_set_enabled(RXPIO, 2, true);
    pio_sm_set_enabled(RXPIO, 0, true);

    // Signal core1 that PIO is started
    core0_started_pio = true;
    __sev();
}

void dreamcast_init(void)
{
    // Configure custom UART pins (12=TX, 13=RX) for debug output
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    stdio_init_all();

    printf("[DC] Initializing Dreamcast Maple Bus output...\n");

    // Initialize controller states
    for (int i = 0; i < MAX_PLAYERS; i++) {
        dc_state[i].buttons = 0xFFFF;
        dc_state[i].rt = 0;
        dc_state[i].lt = 0;
        dc_state[i].joy_x = 128;
        dc_state[i].joy_y = 128;
        dc_state[i].joy2_x = 128;
        dc_state[i].joy2_y = 128;
        dc_rumble[i] = 0;
    }

    // Build pre-built packets
    BuildInfoPacket();
    BuildControllerPacket();
    BuildACKPacket();

    printf("[DC] Maple Bus initialized on GPIO %d/%d\n", MAPLE_PIN1, MAPLE_PIN5);
}

// ============================================================================
// CORE 1 ENTRY (launches RX task)
// ============================================================================

void __not_in_flash_func(dreamcast_core1_task)(void)
{
    core1_rx_task();
    // Never returns
}

// ============================================================================
// CORE 0 TASK (packet processing and TX)
// ============================================================================

void dreamcast_task(void)
{
    static bool setup_done = false;
    static uint32_t start_of_packet = 0;
    static uint32_t packet_count = 0;

    if (!setup_done) {
        // First call - setup TX and RX
        SetupMapleTX();
        SetupMapleRX();
        setup_done = true;
        printf("[DC] Maple TX/RX started\n");
    }


    // Update output state from router
    dreamcast_update_output();

    // Process packets from core1 and send responses
    while (packet_end_read != packet_end_write) {
        // CRITICAL: If we have a pending response, we MUST send it before processing
        // more packets. Otherwise the next packet could overwrite NextPacketSend and
        // we'd lose a response, causing the Dreamcast to think we disconnected.
        if (NextPacketSend != SEND_NOTHING) {
            // Wait for DMA to finish (blocking but necessary for reliable comms)
            while (dma_channel_is_busy(tx_dma_channel)) {
                tight_loop_contents();
            }

            switch (NextPacketSend) {
            case SEND_CONTROLLER_INFO:
                SendPacket((uint32_t *)&InfoPacket, sizeof(InfoPacket) / sizeof(uint32_t));
                break;
            case SEND_CONTROLLER_STATUS:
                SendControllerStatus();
                break;
            case SEND_ACK:
                SendPacket((uint32_t *)&ACKPacket, sizeof(ACKPacket) / sizeof(uint32_t));
                break;
            default:
                break;
            }
            NextPacketSend = SEND_NOTHING;
        }

        uint32_t end_of_packet = packet_ends[packet_end_read];
        packet_end_read = (packet_end_read + 1) & 15;
        packet_count++;

        // Copy and byte-swap packet data
        for (uint32_t j = start_of_packet; j < end_of_packet; j += 4) {
            *(uint32_t *)&Packet[j - start_of_packet] =
                __builtin_bswap32(*(uint32_t *)&RxBuffer[j & (RX_BUFFER_SIZE - 1)]);
        }

        uint32_t packet_size = end_of_packet - start_of_packet;
        ConsumePacket(packet_size);
        start_of_packet = ((end_of_packet + 3) & ~3);
    }

    // Send any final pending response
    if (NextPacketSend != SEND_NOTHING) {
        while (dma_channel_is_busy(tx_dma_channel)) {
            tight_loop_contents();
        }

        switch (NextPacketSend) {
        case SEND_CONTROLLER_INFO:
            SendPacket((uint32_t *)&InfoPacket, sizeof(InfoPacket) / sizeof(uint32_t));
            break;
        case SEND_CONTROLLER_STATUS:
            SendControllerStatus();
            break;
        case SEND_ACK:
            SendPacket((uint32_t *)&ACKPacket, sizeof(ACKPacket) / sizeof(uint32_t));
            break;
        default:
            break;
        }
        NextPacketSend = SEND_NOTHING;
    }
}

// ============================================================================
// FEEDBACK ACCESSORS
// ============================================================================

static uint8_t dc_get_rumble(void)
{
    uint8_t max_rumble = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (dc_rumble[i] > max_rumble) {
            max_rumble = dc_rumble[i];
        }
    }
    return max_rumble;
}

// ============================================================================
// OUTPUT INTERFACE
// ============================================================================

const OutputInterface dreamcast_output_interface = {
    .name = "Dreamcast",
    .target = OUTPUT_TARGET_DREAMCAST,
    .init = dreamcast_init,
    .task = dreamcast_task,
    .core1_task = dreamcast_core1_task,
    .get_feedback = NULL,
    .get_rumble = dc_get_rumble,
    .get_player_led = NULL,
    .get_profile_count = NULL,
    .get_active_profile = NULL,
    .set_active_profile = NULL,
    .get_profile_name = NULL,
    .get_trigger_threshold = NULL,
};
