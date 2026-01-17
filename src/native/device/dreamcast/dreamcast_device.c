// dreamcast_device.c - Dreamcast Maple Bus output interface
// Emulates a Dreamcast controller using PIO for precise timing
//
// Ported from MaplePad by Charlie Cole / mackieks
// https://github.com/mackieks/MaplePad
//
// Architecture (CONFIG_DC_CORE1_TX enabled):
// - Core 1: RX and TX - decodes packets and sends responses immediately
// - Core 0: Updates controller state from router, handles rumble timeouts
//
// PIO allocation:
// - PIO0 SM0: maple_tx (29 instructions)
// - PIO1 SM0-2: maple_rx_triple (10 instructions total)
// - PIO1 SM3: available for other protocols (e.g., N64 joybus at offset 10)

#include "dreamcast_device.h"
#include "maple_state_machine.h"
#include "maple.pio.h"
#include "core/output_interface.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/services/profiles/profile_indicator.h"
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

// Extended device info (for ALL_STATUS_REQUEST)
typedef struct __attribute__((packed)) {
    uint32_t Func;
    uint32_t FuncData[3];
    int8_t AreaCode;
    uint8_t ConnectorDirection;
    char ProductName[30];
    char ProductLicense[60];
    uint16_t StandbyPower;
    uint16_t MaxPower;
    char FreeDeviceStatus[80];  // Extended status string
} PacketAllDeviceInfo;

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

// Puru Puru (vibration) info structure
typedef struct __attribute__((packed)) {
    uint32_t Func;      // Function type (big endian)
    uint8_t VSet0;      // Upper nybble = num vibration sources, lower = location/axis
    uint8_t VSet1;      // b7: Variable intensity, b6: Continuous, b5: Direction, b4: Arbitrary
    uint8_t FMin;       // Minimum frequency (or fixed freq depending on VA mode)
    uint8_t FMax;       // Maximum frequency
} PacketPuruPuruInfo;

// Puru Puru condition structure (for GET_CONDITION response)
typedef struct __attribute__((packed)) {
    uint32_t Func;      // Function type (big endian)
    uint8_t Ctrl;       // Control byte
    uint8_t Power;      // Vibration intensity
    uint8_t Freq;       // Vibration frequency
    uint8_t Inc;        // Vibration inclination
} PacketPuruPuruCondition;

// Pre-built packet types with BitPairsMinus1 prefix for DMA
typedef struct __attribute__((packed)) {
    uint32_t BitPairsMinus1;
    PacketHeader Header;
    PacketDeviceInfo Info;
    uint32_t CRC;
} FInfoPacket;

// Extended info packet (for ALL_STATUS_REQUEST response)
typedef struct __attribute__((packed)) {
    uint32_t BitPairsMinus1;
    PacketHeader Header;
    PacketAllDeviceInfo Info;
    uint32_t CRC;
} FAllInfoPacket;

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

// Puru Puru device info packet
typedef struct __attribute__((packed)) {
    uint32_t BitPairsMinus1;
    PacketHeader Header;
    PacketDeviceInfo Info;
    uint32_t CRC;
} FPuruPuruDeviceInfoPacket;

// Puru Puru media info packet (capabilities)
typedef struct __attribute__((packed)) {
    uint32_t BitPairsMinus1;
    PacketHeader Header;
    PacketPuruPuruInfo Info;
    uint32_t CRC;
} FPuruPuruInfoPacket;

// Puru Puru condition packet
typedef struct __attribute__((packed)) {
    uint32_t BitPairsMinus1;
    PacketHeader Header;
    PacketPuruPuruCondition Condition;
    uint32_t CRC;
} FPuruPuruConditionPacket;

// Puru Puru block read data structure
typedef struct __attribute__((packed)) {
    uint32_t Func;      // Function type (big endian)
    uint32_t Address;   // Block address
    uint8_t Data[4];    // AST data (4 bytes per read)
} PacketPuruPuruBlockRead;

// Puru Puru block read response packet
typedef struct __attribute__((packed)) {
    uint32_t BitPairsMinus1;
    PacketHeader Header;
    PacketPuruPuruBlockRead BlockRead;
    uint32_t CRC;
} FPuruPuruBlockReadPacket;

// ============================================================================
// BUFFERS
// ============================================================================

#define RX_BUFFER_SIZE 4096

static uint8_t RxBuffer[RX_BUFFER_SIZE] __attribute__((aligned(4)));
static uint8_t Packet[1024 + 8] __attribute__((aligned(4)));

// Pre-built response packets
static FInfoPacket InfoPacket;
static FAllInfoPacket AllInfoPacket;
static FAllInfoPacket PuruPuruAllInfoPacket;
static FControllerPacket ControllerPacket;
static FACKPacket ACKPacket;
static FPuruPuruDeviceInfoPacket PuruPuruDeviceInfoPacket;
static FPuruPuruInfoPacket PuruPuruInfoPacket;
static FPuruPuruConditionPacket PuruPuruConditionPacket;
static FPuruPuruBlockReadPacket PuruPuruBlockReadPacket;

// Puru Puru AST (Auto-Stop Table) - default 5 second auto-stop
static uint8_t purupuru_ast[4] = {0x05, 0x00, 0x00, 0x00};

// Puru Puru condition state (what DC last sent us)
static uint8_t purupuru_ctrl[MAX_PLAYERS] = {0};
static uint8_t purupuru_power[MAX_PLAYERS] = {0};
static uint8_t purupuru_freq[MAX_PLAYERS] = {0};
static uint8_t purupuru_inc[MAX_PLAYERS] = {0};

// Deferred rumble update flag - set in ConsumePacket, processed after all packets
// This keeps packet processing fast to avoid DC timeout/disconnection
static volatile bool purupuru_updated[MAX_PLAYERS] = {false};

// Rumble timeout - DC doesn't send explicit "stop", it just stops sending commands
static uint32_t last_rumble_time[MAX_PLAYERS] = {0};
#define RUMBLE_TIMEOUT_MS 300  // Turn off rumble if no command for 300ms

// ============================================================================
// CONTROLLER STATE
// ============================================================================

// Controller state - volatile for cross-core access (Core 0 writes, Core 1 reads)
static volatile dc_controller_state_t dc_state[MAX_PLAYERS];
static uint8_t dc_rumble[MAX_PLAYERS];

// ============================================================================
// SEND STATE
// ============================================================================

typedef enum {
    SEND_NOTHING,
    SEND_CONTROLLER_INFO,
    SEND_CONTROLLER_ALL_INFO,   // Extended device info for controller
    SEND_CONTROLLER_STATUS,
    SEND_ACK,
    SEND_PURUPURU_INFO,         // Device info for Puru Puru
    SEND_PURUPURU_ALL_INFO,     // Extended device info for Puru Puru
    SEND_PURUPURU_MEDIA_INFO,   // Media info (capabilities)
    SEND_PURUPURU_CONDITION,    // Current vibration state
    SEND_PURUPURU_BLOCK_READ,   // AST block read response
} ESendState;

static volatile ESendState NextPacketSend = SEND_NOTHING;

// ============================================================================
// CRC CALCULATION
// ============================================================================

static uint32_t __not_in_flash_func(CalcCRC)(const uint32_t *Words, uint32_t NumWords)
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

// Define combined address for controller + sub-peripherals
#define ADDRESS_CONTROLLER_AND_SUBS (ADDRESS_CONTROLLER | ADDRESS_SUBPERIPHERAL1)

static void BuildInfoPacket(void)
{
    InfoPacket.BitPairsMinus1 = (sizeof(InfoPacket) - 7) * 4 - 1;

    InfoPacket.Header.Command = CMD_RESPOND_DEVICE_STATUS;
    InfoPacket.Header.Destination = ADDRESS_DREAMCAST;
    // Advertise controller + Puru Puru sub-peripheral
    InfoPacket.Header.Origin = ADDRESS_CONTROLLER_AND_SUBS;
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

static void BuildAllInfoPacket(void)
{
    AllInfoPacket.BitPairsMinus1 = (sizeof(AllInfoPacket) - 7) * 4 - 1;

    AllInfoPacket.Header.Command = CMD_RESPOND_ALL_DEVICE_STATUS;
    AllInfoPacket.Header.Destination = ADDRESS_DREAMCAST;
    AllInfoPacket.Header.Origin = ADDRESS_CONTROLLER_AND_SUBS;
    AllInfoPacket.Header.NumWords = sizeof(AllInfoPacket.Info) / sizeof(uint32_t);

    AllInfoPacket.Info.Func = __builtin_bswap32(FUNC_CONTROLLER);
    AllInfoPacket.Info.FuncData[0] = __builtin_bswap32(0x000f06fe);  // Buttons supported
    AllInfoPacket.Info.FuncData[1] = 0;
    AllInfoPacket.Info.FuncData[2] = 0;
    AllInfoPacket.Info.AreaCode = -1;
    AllInfoPacket.Info.ConnectorDirection = 0;
    strncpy(AllInfoPacket.Info.ProductName, "Dreamcast Controller          ", sizeof(AllInfoPacket.Info.ProductName));
    strncpy(AllInfoPacket.Info.ProductLicense,
            "Produced By or Under License From SEGA ENTERPRISES,LTD.     ",
            sizeof(AllInfoPacket.Info.ProductLicense));
    AllInfoPacket.Info.StandbyPower = 430;
    AllInfoPacket.Info.MaxPower = 500;
    strncpy(AllInfoPacket.Info.FreeDeviceStatus,
            "Version 1.010,1998/09/28,315-6125-AB   ,Analog Module : The 4th Edition. 05/08  ",
            sizeof(AllInfoPacket.Info.FreeDeviceStatus));

    AllInfoPacket.CRC = CalcCRC((uint32_t *)&AllInfoPacket.Header, sizeof(AllInfoPacket) / sizeof(uint32_t) - 2);
}

static void BuildPuruPuruAllInfoPacket(void)
{
    PuruPuruAllInfoPacket.BitPairsMinus1 = (sizeof(PuruPuruAllInfoPacket) - 7) * 4 - 1;

    PuruPuruAllInfoPacket.Header.Command = CMD_RESPOND_ALL_DEVICE_STATUS;
    PuruPuruAllInfoPacket.Header.Destination = ADDRESS_DREAMCAST;
    PuruPuruAllInfoPacket.Header.Origin = ADDRESS_SUBPERIPHERAL1;
    PuruPuruAllInfoPacket.Header.NumWords = sizeof(PuruPuruAllInfoPacket.Info) / sizeof(uint32_t);

    PuruPuruAllInfoPacket.Info.Func = __builtin_bswap32(FUNC_VIBRATION);
    PuruPuruAllInfoPacket.Info.FuncData[0] = __builtin_bswap32(0x01010000);
    PuruPuruAllInfoPacket.Info.FuncData[1] = 0;
    PuruPuruAllInfoPacket.Info.FuncData[2] = 0;
    PuruPuruAllInfoPacket.Info.AreaCode = -1;
    PuruPuruAllInfoPacket.Info.ConnectorDirection = 0;
    strncpy(PuruPuruAllInfoPacket.Info.ProductName, "Puru Puru Pack                ", sizeof(PuruPuruAllInfoPacket.Info.ProductName));
    strncpy(PuruPuruAllInfoPacket.Info.ProductLicense,
            "Produced By or Under License From SEGA ENTERPRISES,LTD.     ",
            sizeof(PuruPuruAllInfoPacket.Info.ProductLicense));
    PuruPuruAllInfoPacket.Info.StandbyPower = 200;
    PuruPuruAllInfoPacket.Info.MaxPower = 1600;
    strncpy(PuruPuruAllInfoPacket.Info.FreeDeviceStatus,
            "Version 1.000,1998/11/10,315-6211-AH   ,Vibration Motor:1 , Fm:4 - 30Hz ,Pow:7  ",
            sizeof(PuruPuruAllInfoPacket.Info.FreeDeviceStatus));

    PuruPuruAllInfoPacket.CRC = CalcCRC((uint32_t *)&PuruPuruAllInfoPacket.Header, sizeof(PuruPuruAllInfoPacket) / sizeof(uint32_t) - 2);
}

static void BuildPuruPuruDeviceInfoPacket(void)
{
    PuruPuruDeviceInfoPacket.BitPairsMinus1 = (sizeof(PuruPuruDeviceInfoPacket) - 7) * 4 - 1;

    PuruPuruDeviceInfoPacket.Header.Command = CMD_RESPOND_DEVICE_STATUS;
    PuruPuruDeviceInfoPacket.Header.Destination = ADDRESS_DREAMCAST;
    PuruPuruDeviceInfoPacket.Header.Origin = ADDRESS_SUBPERIPHERAL1;
    PuruPuruDeviceInfoPacket.Header.NumWords = sizeof(PuruPuruDeviceInfoPacket.Info) / sizeof(uint32_t);

    // Puru Puru Pack device info
    PuruPuruDeviceInfoPacket.Info.Func = __builtin_bswap32(FUNC_VIBRATION);  // 0x100
    PuruPuruDeviceInfoPacket.Info.FuncData[0] = __builtin_bswap32(0x01010000);  // Vibration function data
    PuruPuruDeviceInfoPacket.Info.FuncData[1] = 0;
    PuruPuruDeviceInfoPacket.Info.FuncData[2] = 0;
    PuruPuruDeviceInfoPacket.Info.AreaCode = -1;  // All regions
    PuruPuruDeviceInfoPacket.Info.ConnectorDirection = 0;
    strncpy(PuruPuruDeviceInfoPacket.Info.ProductName, "Puru Puru Pack                ", sizeof(PuruPuruDeviceInfoPacket.Info.ProductName));
    strncpy(PuruPuruDeviceInfoPacket.Info.ProductLicense,
            "Produced By or Under License From SEGA ENTERPRISES,LTD.     ",
            sizeof(PuruPuruDeviceInfoPacket.Info.ProductLicense));
    PuruPuruDeviceInfoPacket.Info.StandbyPower = 200;
    PuruPuruDeviceInfoPacket.Info.MaxPower = 1600;

    PuruPuruDeviceInfoPacket.CRC = CalcCRC((uint32_t *)&PuruPuruDeviceInfoPacket.Header, sizeof(PuruPuruDeviceInfoPacket) / sizeof(uint32_t) - 2);
}

static void BuildPuruPuruInfoPacket(void)
{
    PuruPuruInfoPacket.BitPairsMinus1 = (sizeof(PuruPuruInfoPacket) - 7) * 4 - 1;

    PuruPuruInfoPacket.Header.Command = CMD_RESPOND_DATA_TRANSFER;
    PuruPuruInfoPacket.Header.Destination = ADDRESS_DREAMCAST;
    PuruPuruInfoPacket.Header.Origin = ADDRESS_SUBPERIPHERAL1;
    PuruPuruInfoPacket.Header.NumWords = sizeof(PuruPuruInfoPacket.Info) / sizeof(uint32_t);

    PuruPuruInfoPacket.Info.Func = __builtin_bswap32(FUNC_VIBRATION);
    // VSet0: upper nybble = 1 vibration source, lower = 0 (location/axis)
    PuruPuruInfoPacket.Info.VSet0 = 0x10;
    // VSet1: b7=Variable intensity, b6=Continuous, b5=Direction control
    PuruPuruInfoPacket.Info.VSet1 = 0xE0;
    // FMin/FMax: supported frequency range (0x07-0x3B Hz)
    PuruPuruInfoPacket.Info.FMin = 0x07;
    PuruPuruInfoPacket.Info.FMax = 0x3B;

    PuruPuruInfoPacket.CRC = CalcCRC((uint32_t *)&PuruPuruInfoPacket.Header, sizeof(PuruPuruInfoPacket) / sizeof(uint32_t) - 2);
}

static void BuildPuruPuruConditionPacket(void)
{
    PuruPuruConditionPacket.BitPairsMinus1 = (sizeof(PuruPuruConditionPacket) - 7) * 4 - 1;

    PuruPuruConditionPacket.Header.Command = CMD_RESPOND_DATA_TRANSFER;
    PuruPuruConditionPacket.Header.Destination = ADDRESS_DREAMCAST;
    PuruPuruConditionPacket.Header.Origin = ADDRESS_SUBPERIPHERAL1;
    PuruPuruConditionPacket.Header.NumWords = sizeof(PuruPuruConditionPacket.Condition) / sizeof(uint32_t);

    PuruPuruConditionPacket.Condition.Func = __builtin_bswap32(FUNC_VIBRATION);
    PuruPuruConditionPacket.Condition.Ctrl = 0x00;
    PuruPuruConditionPacket.Condition.Power = 0x00;
    PuruPuruConditionPacket.Condition.Freq = 0x00;
    PuruPuruConditionPacket.Condition.Inc = 0x00;

    PuruPuruConditionPacket.CRC = CalcCRC((uint32_t *)&PuruPuruConditionPacket.Header, sizeof(PuruPuruConditionPacket) / sizeof(uint32_t) - 2);
}

static void BuildPuruPuruBlockReadPacket(void)
{
    PuruPuruBlockReadPacket.BitPairsMinus1 = (sizeof(PuruPuruBlockReadPacket) - 7) * 4 - 1;

    PuruPuruBlockReadPacket.Header.Command = CMD_RESPOND_DATA_TRANSFER;
    PuruPuruBlockReadPacket.Header.Destination = ADDRESS_DREAMCAST;
    PuruPuruBlockReadPacket.Header.Origin = ADDRESS_SUBPERIPHERAL1;
    PuruPuruBlockReadPacket.Header.NumWords = sizeof(PuruPuruBlockReadPacket.BlockRead) / sizeof(uint32_t);

    PuruPuruBlockReadPacket.BlockRead.Func = __builtin_bswap32(FUNC_VIBRATION);
    PuruPuruBlockReadPacket.BlockRead.Address = 0;
    memcpy(PuruPuruBlockReadPacket.BlockRead.Data, purupuru_ast, sizeof(PuruPuruBlockReadPacket.BlockRead.Data));

    PuruPuruBlockReadPacket.CRC = CalcCRC((uint32_t *)&PuruPuruBlockReadPacket.Header, sizeof(PuruPuruBlockReadPacket) / sizeof(uint32_t) - 2);
}

static void BuildControllerPacket(void)
{
    ControllerPacket.BitPairsMinus1 = (sizeof(ControllerPacket) - 7) * 4 - 1;

    ControllerPacket.Header.Command = CMD_RESPOND_DATA_TRANSFER;
    ControllerPacket.Header.Destination = ADDRESS_DREAMCAST;
    ControllerPacket.Header.Origin = ADDRESS_CONTROLLER_AND_SUBS;  // Include sub-peripheral bits
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

static void __not_in_flash_func(SendPacket)(const uint32_t *Words, uint32_t NumWords)
{
    // Fix port number in header (doesn't change CRC as same on Origin and Destination)
    PacketHeader *Header = (PacketHeader *)(Words + 1);
    Header->Origin = (Header->Origin & ADDRESS_PERIPHERAL_MASK) | (((PacketHeader *)Packet)->Origin & ADDRESS_PORT_MASK);
    Header->Destination = (Header->Destination & ADDRESS_PERIPHERAL_MASK) | (((PacketHeader *)Packet)->Origin & ADDRESS_PORT_MASK);

    // Wait for any previous DMA to complete first
    while (dma_channel_is_busy(tx_dma_channel)) {
        tight_loop_contents();
    }

    dma_channel_set_read_addr(tx_dma_channel, Words, false);
    dma_channel_set_trans_count(tx_dma_channel, NumWords, true);
}

static void __not_in_flash_func(SendControllerStatus)(void)
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
static volatile uint32_t cmd_purupuru_req = 0;

static bool __not_in_flash_func(ConsumePacket)(uint32_t Size)
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

    // Handle main controller requests (address 0x20)
    if (DestPeripheral == ADDRESS_CONTROLLER) {
        switch (Header->Command) {
        case CMD_RESET_DEVICE:
            // ACK with controller + sub-peripherals
            ACKPacket.Header.Origin = ADDRESS_CONTROLLER_AND_SUBS;
            ACKPacket.CRC = CalcCRC((uint32_t *)&ACKPacket.Header, sizeof(ACKPacket) / sizeof(uint32_t) - 2);
            NextPacketSend = SEND_ACK;
            return true;

        case CMD_DEVICE_REQUEST:
            cmd_device_req++;
            NextPacketSend = SEND_CONTROLLER_INFO;
            return true;

        case CMD_ALL_STATUS_REQUEST:
            // Extended device info with FreeDeviceStatus
            NextPacketSend = SEND_CONTROLLER_ALL_INFO;
            return true;

        case CMD_GET_CONDITION:
            cmd_get_cond++;
            if (Header->NumWords >= 1) {
                uint32_t Func = __builtin_bswap32(PacketData[0]);
                if (Func == FUNC_CONTROLLER) {
                    NextPacketSend = SEND_CONTROLLER_STATUS;
                    return true;
                }
            }
            break;

        default:
            break;
        }
    }
    // Handle Puru Puru (rumble pack) requests (address 0x02)
    else if (DestPeripheral == ADDRESS_SUBPERIPHERAL1) {
        switch (Header->Command) {
        case CMD_RESET_DEVICE:
            ACKPacket.Header.Origin = ADDRESS_SUBPERIPHERAL1;
            ACKPacket.CRC = CalcCRC((uint32_t *)&ACKPacket.Header, sizeof(ACKPacket) / sizeof(uint32_t) - 2);
            NextPacketSend = SEND_ACK;
            return true;

        case CMD_DEVICE_REQUEST:
            // Return Puru Puru device info
            cmd_purupuru_req++;
            NextPacketSend = SEND_PURUPURU_INFO;
            return true;

        case CMD_ALL_STATUS_REQUEST:
            // Extended device info with FreeDeviceStatus
            NextPacketSend = SEND_PURUPURU_ALL_INFO;
            return true;

        case CMD_GET_MEDIA_INFO:
            // Return Puru Puru capabilities
            NextPacketSend = SEND_PURUPURU_MEDIA_INFO;
            return true;

        case CMD_GET_CONDITION:
            // Return current vibration state
            if (Header->NumWords >= 1) {
                uint32_t Func = __builtin_bswap32(PacketData[0]);
                if (Func == FUNC_VIBRATION) {
                    // Update condition packet with current state
                    PuruPuruConditionPacket.Condition.Ctrl = purupuru_ctrl[0];
                    PuruPuruConditionPacket.Condition.Power = purupuru_power[0];
                    PuruPuruConditionPacket.Condition.Freq = purupuru_freq[0];
                    PuruPuruConditionPacket.Condition.Inc = purupuru_inc[0];
                    PuruPuruConditionPacket.CRC = CalcCRC((uint32_t *)&PuruPuruConditionPacket.Header,
                        sizeof(PuruPuruConditionPacket) / sizeof(uint32_t) - 2);
                    NextPacketSend = SEND_PURUPURU_CONDITION;
                    return true;
                }
            }
            break;

        case CMD_SET_CONDITION:
            if (Header->NumWords >= 2) {
                uint32_t Func = __builtin_bswap32(PacketData[0]);
                if (Func == FUNC_VIBRATION) {
                    // Extract rumble data
                    uint8_t *CondData = (uint8_t *)&PacketData[1];
                    purupuru_ctrl[0] = CondData[0];
                    purupuru_power[0] = CondData[1];
                    purupuru_freq[0] = CondData[2];
                    purupuru_inc[0] = CondData[3];

                    // Record timestamp for auto-stop timeout
                    // app_task() will read dc_get_rumble() and set feedback
                    bool rumble_on = (purupuru_ctrl[0] & 0x10) &&
                                     (purupuru_freq[0] >= 0x07) &&
                                     (purupuru_freq[0] <= 0x3B);
                    if (rumble_on) {
                        last_rumble_time[0] = time_us_32() / 1000;  // ms
                    } else {
                        last_rumble_time[0] = 0;  // Stop rumble
                    }

                    // Send ACK from Puru Puru address (must match MaplePad)
                    ACKPacket.Header.Origin = ADDRESS_SUBPERIPHERAL1;
                    ACKPacket.CRC = CalcCRC((uint32_t *)&ACKPacket.Header, sizeof(ACKPacket) / sizeof(uint32_t) - 2);
                    NextPacketSend = SEND_ACK;
                    return true;
                }
            }
            break;

        case CMD_BLOCK_READ:
            // Puru Puru AST (Auto Stop Table) read - return AST data
            NextPacketSend = SEND_PURUPURU_BLOCK_READ;
            return true;

        case CMD_BLOCK_WRITE:
            // Puru Puru AST (Auto Stop Table) write - store and ACK
            if (Header->NumWords >= 2) {
                // Data starts at PacketData[2] (after Func and Address)
                uint8_t *WriteData = (uint8_t *)&PacketData[2];
                // Copy up to 4 bytes of AST data
                uint8_t bytes_to_copy = (Header->NumWords - 2) * 4;
                if (bytes_to_copy > sizeof(purupuru_ast)) {
                    bytes_to_copy = sizeof(purupuru_ast);
                }
                memcpy(purupuru_ast, WriteData, bytes_to_copy);
            }
            ACKPacket.Header.Origin = ADDRESS_SUBPERIPHERAL1;
            ACKPacket.CRC = CalcCRC((uint32_t *)&ACKPacket.Header, sizeof(ACKPacket) / sizeof(uint32_t) - 2);
            NextPacketSend = SEND_ACK;
            return true;

        default:
            break;
        }
    }

    return false;
}

// ============================================================================
// BUTTON MAPPING
// ============================================================================

static uint16_t map_buttons_to_dc(uint32_t jp_buttons)
{
    uint16_t dc_buttons = 0;

    // Face buttons B1-B4 -> A, B, X, Y
    if (jp_buttons & JP_BUTTON_B1) dc_buttons |= DC_BTN_A;
    if (jp_buttons & JP_BUTTON_B2) dc_buttons |= DC_BTN_B;
    if (jp_buttons & JP_BUTTON_B3) dc_buttons |= DC_BTN_X;
    if (jp_buttons & JP_BUTTON_B4) dc_buttons |= DC_BTN_Y;

    // L2/R2 -> triggers (handled in analog section)
    // R1 (N64 Z, GC Z) -> D button (extra action button)
    if (jp_buttons & JP_BUTTON_R1) dc_buttons |= DC_BTN_D;

    // L3/R3 -> extra face buttons Z/C
    if (jp_buttons & JP_BUTTON_L3) dc_buttons |= DC_BTN_Z;
    if (jp_buttons & JP_BUTTON_R3) dc_buttons |= DC_BTN_C;

    // S1 -> D (also), S2 -> Start
    if (jp_buttons & JP_BUTTON_S1) dc_buttons |= DC_BTN_D;
    if (jp_buttons & JP_BUTTON_S2) dc_buttons |= DC_BTN_START;

    // D-pad
    if (jp_buttons & JP_BUTTON_DU) dc_buttons |= DC_BTN_UP;
    if (jp_buttons & JP_BUTTON_DD) dc_buttons |= DC_BTN_DOWN;
    if (jp_buttons & JP_BUTTON_DL) dc_buttons |= DC_BTN_LEFT;
    if (jp_buttons & JP_BUTTON_DR) dc_buttons |= DC_BTN_RIGHT;

    // A1 (guide) -> Start
    if (jp_buttons & JP_BUTTON_A1) dc_buttons |= DC_BTN_START;

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

        // L trigger: L2 (analog + digital) - consistent with GC/N64 mapping
        // N64 L -> L2, GC L -> L2, USB L2 -> L2
        uint8_t lt = event->analog[ANALOG_L2];
        if (event->buttons & JP_BUTTON_L2) lt = 255;
        dc_state[port].lt = lt;

        // R trigger: R2 (analog + digital) - consistent with GC/N64 mapping
        // N64 R -> R2, GC R -> R2, USB R2 -> R2
        uint8_t rt = event->analog[ANALOG_R2];
        if (event->buttons & JP_BUTTON_R2) rt = 255;
        dc_state[port].rt = rt;
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
static volatile uint32_t tx_sent_count = 0;  // TX responses sent
static volatile uint32_t tx_busy_count = 0;  // TX skipped (DMA busy)
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

#ifdef CONFIG_DC_CORE1_TX
                // Process and respond IMMEDIATELY on Core 1 (like GameCube)
                // Copy and byte-swap packet data from RxBuffer to Packet buffer
                // (ConsumePacket reads from Packet, not RxBuffer)
                uint32_t packet_size = Offset - StartOfPacket;
                for (uint32_t j = StartOfPacket; j < Offset; j += 4) {
                    *(uint32_t *)&Packet[j - StartOfPacket] =
                        __builtin_bswap32(*(uint32_t *)&RxBuffer[j & (RX_BUFFER_SIZE - 1)]);
                }

                // Process packet and prepare response
                ConsumePacket(packet_size);

                // Send response immediately via DMA
                if (NextPacketSend != SEND_NOTHING) {
                    if (dma_channel_is_busy(tx_dma_channel)) {
                        tx_busy_count++;
                        // If DMA has been stuck for many packets, abort and reset it
                        if (tx_busy_count > 50 && (tx_busy_count % 100) == 0) {
                            dma_channel_abort(tx_dma_channel);
                            // Small delay for abort to complete
                            for (volatile int i = 0; i < 100; i++) {}
                        }
                    }
                    if (!dma_channel_is_busy(tx_dma_channel)) {
                        tx_sent_count++;
                        switch (NextPacketSend) {
                        case SEND_CONTROLLER_INFO:
                            SendPacket((uint32_t *)&InfoPacket, sizeof(InfoPacket) / sizeof(uint32_t));
                            break;
                        case SEND_CONTROLLER_ALL_INFO:
                            SendPacket((uint32_t *)&AllInfoPacket, sizeof(AllInfoPacket) / sizeof(uint32_t));
                            break;
                        case SEND_CONTROLLER_STATUS:
                            SendControllerStatus();
                            break;
                        case SEND_ACK:
                            SendPacket((uint32_t *)&ACKPacket, sizeof(ACKPacket) / sizeof(uint32_t));
                            break;
                        case SEND_PURUPURU_INFO:
                            SendPacket((uint32_t *)&PuruPuruDeviceInfoPacket, sizeof(PuruPuruDeviceInfoPacket) / sizeof(uint32_t));
                            break;
                        case SEND_PURUPURU_ALL_INFO:
                            SendPacket((uint32_t *)&PuruPuruAllInfoPacket, sizeof(PuruPuruAllInfoPacket) / sizeof(uint32_t));
                            break;
                        case SEND_PURUPURU_MEDIA_INFO:
                            SendPacket((uint32_t *)&PuruPuruInfoPacket, sizeof(PuruPuruInfoPacket) / sizeof(uint32_t));
                            break;
                        case SEND_PURUPURU_CONDITION:
                            SendPacket((uint32_t *)&PuruPuruConditionPacket, sizeof(PuruPuruConditionPacket) / sizeof(uint32_t));
                            break;
                        case SEND_PURUPURU_BLOCK_READ:
                            SendPacket((uint32_t *)&PuruPuruBlockReadPacket, sizeof(PuruPuruBlockReadPacket) / sizeof(uint32_t));
                            break;
                        default:
                            break;
                        }
                    }
                    NextPacketSend = SEND_NOTHING;
                }
#else
                // Queue packet for Core 0 processing
                packet_ends[packet_end_write] = Offset;
                packet_end_write = (packet_end_write + 1) & 15;
#endif

                StartOfPacket = ((Offset + 3) & ~3);  // Align for next packet
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
    // Claim SM0-2 for maple_rx before use
    for (int sm = 0; sm < 3; sm++) {
        pio_sm_claim(RXPIO, sm);
    }

    uint offsets[3];
    offsets[0] = pio_add_program(RXPIO, &maple_rx_triple1_program);
    offsets[1] = pio_add_program(RXPIO, &maple_rx_triple2_program);
    offsets[2] = pio_add_program(RXPIO, &maple_rx_triple3_program);
    printf("[DC] maple_rx offsets: %d, %d, %d (sizes: %d, %d, %d)\n",
           offsets[0], offsets[1], offsets[2],
           maple_rx_triple1_program.length, maple_rx_triple2_program.length,
           maple_rx_triple3_program.length);

    // Clock divider of 3.0 (from MaplePad)
    maple_rx_triple_program_init(RXPIO, offsets, MAPLE_PIN1, MAPLE_PIN5, 3.0f);

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

    // Disable profile indicator rumble - DC manages its own rumble from console
    profile_indicator_disable_rumble();

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
    BuildAllInfoPacket();
    BuildControllerPacket();
    BuildACKPacket();
    BuildPuruPuruDeviceInfoPacket();
    BuildPuruPuruAllInfoPacket();
    BuildPuruPuruInfoPacket();
    BuildPuruPuruConditionPacket();
    BuildPuruPuruBlockReadPacket();

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
    static uint32_t last_debug_time = 0;
    static uint32_t last_rx_ends = 0;

    if (!setup_done) {
        // First call - setup TX and RX
        printf("[DC] Setting up Maple TX (PIO0)...\n");
        SetupMapleTX();
        printf("[DC] Setting up Maple RX (PIO1)...\n");
        SetupMapleRX();
        setup_done = true;
        printf("[DC] Maple TX/RX started\n");
        last_debug_time = time_us_32();
    }

    // Periodic debug output (every 5 seconds)
    uint32_t now = time_us_32();
    if (now - last_debug_time > 5000000) {
        uint32_t rx_delta = rx_ends_count - last_rx_ends;
        // Check TX PIO and DMA state
        uint32_t tx_fifo_level = pio_sm_get_tx_fifo_level(TXPIO, tx_sm);
        uint32_t dma_remaining = dma_channel_hw_addr(tx_dma_channel)->transfer_count;
        bool dma_busy = dma_channel_is_busy(tx_dma_channel);
        printf("[DC] t=%lus: rx=%lu(+%lu) tx=%lu busy=%lu fifo=%lu dma_remain=%lu dma_busy=%d\n",
               now / 1000000, rx_ends_count, rx_delta, tx_sent_count, tx_busy_count,
               tx_fifo_level, dma_remaining, dma_busy);
        last_rx_ends = rx_ends_count;
        last_debug_time = now;
    }

    // CRITICAL: Process packets FIRST - this is time-sensitive!
    // The DC expects responses within a tight window.
    // All other processing (rumble, router updates) must wait.
    // MaplePad order: process packet -> send response -> next packet
    // This minimizes latency between receiving and responding
    while (packet_end_read != packet_end_write) {
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

        // Send response IMMEDIATELY after processing (like MaplePad)
        // Don't wait for DMA - if it's busy, we're already too slow
        if (NextPacketSend != SEND_NOTHING && !dma_channel_is_busy(tx_dma_channel)) {
            switch (NextPacketSend) {
            case SEND_CONTROLLER_INFO:
                SendPacket((uint32_t *)&InfoPacket, sizeof(InfoPacket) / sizeof(uint32_t));
                break;
            case SEND_CONTROLLER_ALL_INFO:
                SendPacket((uint32_t *)&AllInfoPacket, sizeof(AllInfoPacket) / sizeof(uint32_t));
                break;
            case SEND_CONTROLLER_STATUS:
                SendControllerStatus();
                break;
            case SEND_ACK:
                SendPacket((uint32_t *)&ACKPacket, sizeof(ACKPacket) / sizeof(uint32_t));
                break;
            case SEND_PURUPURU_INFO:
                SendPacket((uint32_t *)&PuruPuruDeviceInfoPacket, sizeof(PuruPuruDeviceInfoPacket) / sizeof(uint32_t));
                break;
            case SEND_PURUPURU_ALL_INFO:
                SendPacket((uint32_t *)&PuruPuruAllInfoPacket, sizeof(PuruPuruAllInfoPacket) / sizeof(uint32_t));
                break;
            case SEND_PURUPURU_MEDIA_INFO:
                SendPacket((uint32_t *)&PuruPuruInfoPacket, sizeof(PuruPuruInfoPacket) / sizeof(uint32_t));
                break;
            case SEND_PURUPURU_CONDITION:
                SendPacket((uint32_t *)&PuruPuruConditionPacket, sizeof(PuruPuruConditionPacket) / sizeof(uint32_t));
                break;
            case SEND_PURUPURU_BLOCK_READ:
                SendPacket((uint32_t *)&PuruPuruBlockReadPacket, sizeof(PuruPuruBlockReadPacket) / sizeof(uint32_t));
                break;
            default:
                break;
            }
            NextPacketSend = SEND_NOTHING;
        }
    }

    // Handle any remaining pending response (shouldn't happen normally)
    if (NextPacketSend != SEND_NOTHING && !dma_channel_is_busy(tx_dma_channel)) {
        switch (NextPacketSend) {
        case SEND_CONTROLLER_INFO:
            SendPacket((uint32_t *)&InfoPacket, sizeof(InfoPacket) / sizeof(uint32_t));
            break;
        case SEND_CONTROLLER_ALL_INFO:
            SendPacket((uint32_t *)&AllInfoPacket, sizeof(AllInfoPacket) / sizeof(uint32_t));
            break;
        case SEND_CONTROLLER_STATUS:
            SendControllerStatus();
            break;
        case SEND_ACK:
            SendPacket((uint32_t *)&ACKPacket, sizeof(ACKPacket) / sizeof(uint32_t));
            break;
        case SEND_PURUPURU_INFO:
            SendPacket((uint32_t *)&PuruPuruDeviceInfoPacket, sizeof(PuruPuruDeviceInfoPacket) / sizeof(uint32_t));
            break;
        case SEND_PURUPURU_ALL_INFO:
            SendPacket((uint32_t *)&PuruPuruAllInfoPacket, sizeof(PuruPuruAllInfoPacket) / sizeof(uint32_t));
            break;
        case SEND_PURUPURU_MEDIA_INFO:
            SendPacket((uint32_t *)&PuruPuruInfoPacket, sizeof(PuruPuruInfoPacket) / sizeof(uint32_t));
            break;
        case SEND_PURUPURU_CONDITION:
            SendPacket((uint32_t *)&PuruPuruConditionPacket, sizeof(PuruPuruConditionPacket) / sizeof(uint32_t));
            break;
        case SEND_PURUPURU_BLOCK_READ:
            SendPacket((uint32_t *)&PuruPuruBlockReadPacket, sizeof(PuruPuruBlockReadPacket) / sizeof(uint32_t));
            break;
        default:
            break;
        }
        NextPacketSend = SEND_NOTHING;
    }

    // AFTER packet processing: Update router state and handle rumble
    // These are lower priority than responding to DC commands
    dreamcast_update_output();

    // Rumble timeout check - auto-stop if no new commands received
    uint32_t now_ms = time_us_32() / 1000;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        purupuru_updated[i] = false;

        // Check if rumble should auto-stop due to timeout
        if (last_rumble_time[i] != 0) {
            uint32_t elapsed = now_ms - last_rumble_time[i];
            if (elapsed > RUMBLE_TIMEOUT_MS) {
                // Timeout expired - turn off rumble
                feedback_set_rumble_internal(i, 0, 0);
                last_rumble_time[i] = 0;
            }
        }
    }

}


// ============================================================================
// FEEDBACK ACCESSORS
// ============================================================================

// Get raw Puru Puru state for a port (can be called from app_task)
// Returns true if rumble should be active
bool dreamcast_get_purupuru_state(uint8_t port, uint8_t* power, uint8_t* freq, uint8_t* inc)
{
    if (port >= MAX_PLAYERS) return false;

    // ctrl bit 4 (0x10) = vibration enable
    bool enabled = (purupuru_ctrl[port] & 0x10) != 0;

    if (power) *power = purupuru_power[port];
    if (freq) *freq = purupuru_freq[port];
    if (inc) *inc = purupuru_inc[port];

    return enabled;
}

bool dreamcast_has_pending_packets(void)
{
    return packet_end_read != packet_end_write;
}

static uint8_t dc_get_rumble(void)
{
    // Check if rumble is active (not timed out)
    if (last_rumble_time[0] == 0) {
        return 0;
    }

    // Return the current rumble intensity
    // ctrl bit 4 (0x10) = enable, freq must be in valid range
    bool rumble_on = (purupuru_ctrl[0] & 0x10) &&
                     (purupuru_freq[0] >= 0x07) &&
                     (purupuru_freq[0] <= 0x3B);
    if (!rumble_on) {
        return 0;
    }

    // Scale power (0-7 typical, but can be higher) to 0-255
    // Use same calculation as in SET_CONDITION handler
    return (purupuru_power[0] * 36 > 255) ? 255 : (purupuru_power[0] * 36);
}

// ============================================================================
// DIRECT STATE UPDATE (for low-latency input sources like N64)
// ============================================================================

void dreamcast_set_controller_state(uint8_t port, uint16_t buttons,
                                     uint8_t joy_x, uint8_t joy_y,
                                     uint8_t joy2_x, uint8_t joy2_y,
                                     uint8_t lt, uint8_t rt)
{
    if (port >= MAX_PLAYERS) return;

    // Direct write to volatile state - Core 1 reads this immediately
    dc_state[port].buttons = buttons;
    dc_state[port].joy_x = joy_x;
    dc_state[port].joy_y = joy_y;
    dc_state[port].joy2_x = joy2_x;
    dc_state[port].joy2_y = joy2_y;
    dc_state[port].lt = lt;
    dc_state[port].rt = rt;
}

uint8_t dreamcast_get_rumble(uint8_t port)
{
    if (port >= MAX_PLAYERS) return 0;
    return dc_rumble[port];
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
