// vmu.c - Dreamcast VMU emulation for JoypadOS
// 128KB VMU image in RAM, persisted to SD card (DC_1.VMU) via vmu_sd.c.

#include "vmu.h"
#include "vmu_sd.h"
#include "vmu_storage.h"
#include "vmu_default_icondata.h"
#include "dreamcast_device.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

#define CMD_RESPOND_DEVICE_STATUS       5
#define CMD_RESPOND_ALL_DEVICE_STATUS   6
#define CMD_RESPOND_DATA_TRANSFER       8
#define FUNC_MEMORY     0x00000002
#define FUNC_LCD        0x00000004
#define FUNC_TIMER      0x00000008
#define VMU_ROOT_BLOCK              255
#define VMU_FAT_BLOCK               254
#define VMU_NUM_FAT_BLOCKS          1
#define VMU_DIRECTORY_BLOCK         253
#define VMU_NUM_DIRECTORY_BLOCKS    13
#define VMU_SAVE_BLOCK              200
#define VMU_NUM_SAVE_BLOCKS         31

// 128KB RAM buffer for VMU image
// NOTE: RP2040 has 264KB SRAM total — JoypadOS uses ~50KB so this fits
uint8_t vmu_ram[VMU_TOTAL_BLOCKS * VMU_BLOCK_SIZE];
volatile bool vmu_dirty_flag = false;  // Set by Core 1, read by Core 0

static uint8_t write_buf[VMU_BLOCK_SIZE] __attribute__((aligned(4)));
static uint16_t write_block  = 0xFFFF;
static uint8_t  write_phases = 0;
static uint8_t   vmu_port_addr = 0x01;
static VmuStatus vmu_status = VMU_STATUS_OK;

typedef struct __attribute__((packed)) {
    uint32_t BitPairsMinus1;
    uint8_t  Command; uint8_t  Destination; uint8_t  Origin; uint8_t  NumWords;
    uint32_t Func; uint32_t FuncData[3];
    int8_t   AreaCode; uint8_t  ConnectorDirection;
    char     ProductName[30]; char     ProductLicense[60];
    uint16_t StandbyPower; uint16_t MaxPower;
    uint32_t CRC;
} VmuInfoPkt;

typedef struct __attribute__((packed)) {
    uint32_t BitPairsMinus1;
    uint8_t  Command; uint8_t  Destination; uint8_t  Origin; uint8_t  NumWords;
    uint32_t Func; uint32_t FuncData[3];
    int8_t   AreaCode; uint8_t  ConnectorDirection;
    char     ProductName[30]; char     ProductLicense[60];
    uint16_t StandbyPower; uint16_t MaxPower;
    char     FreeDeviceStatus[80];
    uint32_t CRC;
} VmuAllInfoPkt;

typedef struct __attribute__((packed)) {
    uint32_t BitPairsMinus1;
    uint8_t  Command; uint8_t  Destination; uint8_t  Origin; uint8_t  NumWords;
    uint32_t Func;
    uint16_t TotalSize; uint16_t PartitionNumber;
    uint16_t SystemArea; uint16_t FATArea;
    uint16_t NumFATBlocks; uint16_t FileInfoArea;
    uint16_t NumInfoBlocks; uint8_t VolumeIcon; uint8_t Reserved;
    uint16_t SaveArea; uint16_t NumSaveBlocks;
    uint32_t Reserved32;
    uint16_t Reserved16;
    uint16_t Padding;    // Alignment padding — matches MaplePad's unpadded struct size
    uint32_t CRC;
} VmuMediaInfoPkt;

typedef struct __attribute__((packed)) {
    uint32_t BitPairsMinus1;
    uint8_t  Command; uint8_t  Destination; uint8_t  Origin; uint8_t  NumWords;
    uint32_t Func; uint16_t Address; uint8_t Phase; uint8_t Reserved;
    uint8_t  Data[VMU_BLOCK_SIZE];
    uint32_t CRC;
} VmuBlockReadPkt;

typedef struct __attribute__((packed)) {
    uint32_t BitPairsMinus1;
    uint8_t  Command; uint8_t  Destination; uint8_t  Origin; uint8_t  NumWords;
    uint32_t CRC;
} VmuAckPkt;

static VmuInfoPkt      vmu_info_pkt;
static VmuAllInfoPkt   vmu_all_info_pkt;
static VmuMediaInfoPkt vmu_media_pkt;
static VmuBlockReadPkt vmu_block_read_pkt;
static VmuAckPkt       vmu_ack_pkt;

static uint32_t __not_in_flash_func(CalcCRC)(const uint32_t *w, uint32_t n) {
    uint32_t x=0; for(uint32_t i=0;i<n;i++) x^=w[i]; x^=(x<<16); x^=(x<<8); return x;
}

void vmu_build_packets(uint8_t port_addr) {
    vmu_port_addr = port_addr;
    { VmuInfoPkt *p=&vmu_info_pkt;
      p->BitPairsMinus1=(sizeof(*p)-7)*4-1; p->Command=CMD_RESPOND_DEVICE_STATUS;
      p->Destination=0; p->Origin=port_addr;
      p->NumWords=(sizeof(*p)-sizeof(p->BitPairsMinus1)-sizeof(p->CRC)-sizeof(uint32_t))/sizeof(uint32_t);
      p->Func=__builtin_bswap32(FUNC_MEMORY|FUNC_LCD|FUNC_TIMER);
      p->FuncData[0]=__builtin_bswap32(0x7E7E3F40); p->FuncData[1]=__builtin_bswap32(0x00051000); p->FuncData[2]=__builtin_bswap32(0x000f4100);
      p->AreaCode=-1; p->ConnectorDirection=0;
      strncpy(p->ProductName,"Visual Memory                 ",sizeof(p->ProductName));
      strncpy(p->ProductLicense,"Produced By or Under License From SEGA ENTERPRISES,LTD.     ",sizeof(p->ProductLicense));
      p->StandbyPower=100; p->MaxPower=130;
      p->CRC=CalcCRC((uint32_t*)&p->Command,(sizeof(*p)-sizeof(p->BitPairsMinus1)-sizeof(p->CRC))/sizeof(uint32_t)); }
    { VmuAllInfoPkt *p=&vmu_all_info_pkt;
      p->BitPairsMinus1=(sizeof(*p)-7)*4-1; p->Command=CMD_RESPOND_ALL_DEVICE_STATUS;
      p->Destination=0; p->Origin=port_addr;
      p->NumWords=(sizeof(*p)-sizeof(p->BitPairsMinus1)-sizeof(p->CRC)-sizeof(uint32_t))/sizeof(uint32_t);
      p->Func=__builtin_bswap32(FUNC_MEMORY|FUNC_LCD|FUNC_TIMER);
      p->FuncData[0]=__builtin_bswap32(0x7E7E3F40); p->FuncData[1]=__builtin_bswap32(0x00051000); p->FuncData[2]=__builtin_bswap32(0x000f4100);
      p->AreaCode=-1; p->ConnectorDirection=0;
      strncpy(p->ProductName,"Visual Memory                 ",sizeof(p->ProductName));
      strncpy(p->ProductLicense,"Produced By or Under License From SEGA ENTERPRISES,LTD.     ",sizeof(p->ProductLicense));
      p->StandbyPower=100; p->MaxPower=130;
      strncpy(p->FreeDeviceStatus,"Version 1.005,1999/10/26,315-6208-05,SEGA Visual Memory System BIOS Produced by ",sizeof(p->FreeDeviceStatus));
      p->CRC=CalcCRC((uint32_t*)&p->Command,(sizeof(*p)-sizeof(p->BitPairsMinus1)-sizeof(p->CRC))/sizeof(uint32_t)); }
    { VmuMediaInfoPkt *p=&vmu_media_pkt;
      p->BitPairsMinus1=(sizeof(*p)-7)*4-1; p->Command=CMD_RESPOND_DATA_TRANSFER;
      p->Destination=0; p->Origin=port_addr;
      p->NumWords=(sizeof(*p)-sizeof(p->BitPairsMinus1)-sizeof(p->CRC)-sizeof(uint32_t))/sizeof(uint32_t);
      p->Func=__builtin_bswap32(FUNC_MEMORY);
      p->TotalSize=VMU_TOTAL_BLOCKS-1; p->PartitionNumber=0;
      p->SystemArea=VMU_ROOT_BLOCK; p->FATArea=VMU_FAT_BLOCK;
      p->NumFATBlocks=VMU_NUM_FAT_BLOCKS; p->FileInfoArea=VMU_DIRECTORY_BLOCK;
      p->NumInfoBlocks=VMU_NUM_DIRECTORY_BLOCKS; p->VolumeIcon=1; p->Reserved=0;
      p->SaveArea=VMU_SAVE_BLOCK; p->NumSaveBlocks=VMU_NUM_SAVE_BLOCKS;
      p->Reserved32=0x00800000;
      p->Reserved16=0;
      p->Padding=0;
      p->CRC=CalcCRC((uint32_t*)&p->Command,(sizeof(*p)-sizeof(p->BitPairsMinus1)-sizeof(p->CRC))/sizeof(uint32_t)); }
    { VmuBlockReadPkt *p=&vmu_block_read_pkt;
      p->Command=CMD_RESPOND_DATA_TRANSFER; p->Destination=0; p->Origin=port_addr;
      p->Func=__builtin_bswap32(FUNC_MEMORY); p->Phase=0; p->Reserved=0; }
    { VmuAckPkt *p=&vmu_ack_pkt;
      p->BitPairsMinus1=(sizeof(*p)-7)*4-1; p->Command=7;
      p->Destination=0; p->Origin=port_addr; p->NumWords=0;
      p->CRC=CalcCRC((uint32_t*)&p->Command,(sizeof(*p)-sizeof(p->BitPairsMinus1)-sizeof(p->CRC))/sizeof(uint32_t)); }
    printf("[VMU] Packets built, RAM storage (%dKB)\n", sizeof(vmu_ram)/1024);
}

// Pre-format vmu_ram exactly like MaplePad's CheckFormatted()
// The DC sees an already-formatted card and skips the format operation.
// Also drops a default ICONDATA_VMS file so the freshly-initialized card
// shows a Joypad OS logo in the DC BIOS instead of the no-icon placeholder.
static void vmu_preformat(void) {
    const uint16_t ROOT_BLOCK = 255, FAT_BLOCK = 254, DIR_BLOCK = 253, DIR_SIZE = 13;
    uint16_t start_sys = DIR_BLOCK - DIR_SIZE + 1; // 241

    // Zero system area (blocks 241-255)
    memset(&vmu_ram[start_sys * VMU_BLOCK_SIZE], 0, (256 - start_sys) * VMU_BLOCK_SIZE);

    // Root block (255): magic + filesystem layout
    uint8_t *root = &vmu_ram[ROOT_BLOCK * VMU_BLOCK_SIZE];
    memset(root, 0x55, 16);  // Magic
    // Format timestamp at 0x30
    static const uint8_t ts[] = {0x20,0x21,0x03,0x02,0x09,0x00,0x00,0x01};
    memcpy(root + 0x30, ts, 8);
    // Filesystem info at 0x44 (LE16 fields)
    uint16_t *r = (uint16_t *)(root + 0x44);
    r[0]=254; r[1]=0; r[2]=255; r[3]=254; r[4]=1; r[5]=253; r[6]=13; r[7]=0; r[8]=200; r[9]=31;
    // Reserved32 = 0x00800000 at offset 0x60
    root[0x60]=0x00; root[0x61]=0x00; root[0x62]=0x80; root[0x63]=0x00;

    // FAT block (254): free area + system marks
    uint16_t *fat = (uint16_t *)&vmu_ram[FAT_BLOCK * VMU_BLOCK_SIZE];
    for (int i = 0; i < 241; i++) fat[i] = 0xFFFC;  // free
    for (int i = 241; i < 256; i++) fat[i] = 0xFFFA;  // system (EOF)

    // --- Default ICONDATA_VMS ----------------------------------------------
    // Occupies the first two save-area blocks (0, 1) and is described by a
    // single DATA-type directory entry. The DC BIOS picks it up by filename
    // ("ICONDATA_VMS") and renders the mono + 16-color icons it contains.
    // Save data written by games claims free blocks starting from the top
    // (block 240 down), so reserving the bottom 2 blocks doesn't crowd them.
    memcpy(&vmu_ram[0 * VMU_BLOCK_SIZE], vmu_default_icondata,
           VMU_DEFAULT_ICONDATA_SIZE);
    fat[0] = 0x0001;   // block 0 -> next is block 1
    fat[1] = 0xFFFA;   // block 1 -> EOF

    // Directory entry 0 lives at the start of block 253. 32 bytes wide.
    uint8_t *dir = &vmu_ram[DIR_BLOCK * VMU_BLOCK_SIZE];
    dir[0x00] = 0x33;                              // file type: DATA
    dir[0x01] = 0x00;                              // copy protection: none
    dir[0x02] = 0x00; dir[0x03] = 0x00;            // first block (LE16) = 0
    memcpy(&dir[0x04], "ICONDATA_VMS", 12);        // 12-byte filename
    memcpy(&dir[0x10], ts, 8);                     // BCD creation timestamp
    dir[0x18] = 0x02; dir[0x19] = 0x00;            // file size in blocks (LE16) = 2
    dir[0x1A] = 0x00; dir[0x1B] = 0x00;            // header offset (LE16) = 0
    dir[0x1C] = 0x00; dir[0x1D] = 0x00;            // reserved
    dir[0x1E] = 0x00; dir[0x1F] = 0x00;
}

void vmu_init(uint8_t port_addr) {
    vmu_port_addr=port_addr; vmu_status=VMU_STATUS_OK;
    write_block=0xFFFF; write_phases=0;
    memset(vmu_ram, 0xFF, sizeof(vmu_ram));
    vmu_preformat();
    // SD init deferred to vmu_sd_load() — called after Maple Bus enumeration
    // to avoid blocking DC detection during boot
    printf("[VMU] Initialized (SD load deferred)\n");
}

// Called after controller enumerates with DC — selects a persistence backend
// (USB flash > SD > QSPI > RAM) and loads any saved image. Deferred from
// vmu_init() to avoid blocking Maple Bus enumeration.
void vmu_sd_load(void) {
    vmu_storage_init();
    printf("[VMU] Storage load complete\n");
}

void vmu_set_slot(uint8_t slot)  { (void)slot; }
uint8_t   vmu_get_slot(void)     { return 1; }
VmuStatus vmu_get_status(void)   { return vmu_status; }
uint8_t   vmu_get_address(void)  { return vmu_port_addr; }

const void* __not_in_flash_func(vmu_get_device_info_packet)(uint32_t *sz)     { *sz=sizeof(vmu_info_pkt)/4;      return &vmu_info_pkt; }
const void* __not_in_flash_func(vmu_get_all_device_info_packet)(uint32_t *sz) { *sz=sizeof(vmu_all_info_pkt)/4;  return &vmu_all_info_pkt; }
const void* __not_in_flash_func(vmu_get_media_info_packet)(uint32_t *sz)      { *sz=sizeof(vmu_media_pkt)/4;     return &vmu_media_pkt; }
const void* __not_in_flash_func(vmu_get_block_read_packet)(uint32_t *sz)      { *sz=sizeof(vmu_block_read_pkt)/4; return &vmu_block_read_pkt; }
const void* __not_in_flash_func(vmu_get_ack_packet)(uint32_t *sz)             { *sz=sizeof(vmu_ack_pkt)/4;       return &vmu_ack_pkt; }

const void* __not_in_flash_func(vmu_handle_block_read)(const uint32_t *pkt, uint32_t *sz) {
    uint16_t block = (uint16_t)((pkt[1] >> 24) & 0xFF);
    if (block >= VMU_TOTAL_BLOCKS) block = 0;
    VmuBlockReadPkt *p = &vmu_block_read_pkt;
    p->Address = block;
    memcpy(p->Data, vmu_ram + (uint32_t)block * VMU_BLOCK_SIZE, VMU_BLOCK_SIZE);
    p->BitPairsMinus1 = (sizeof(*p)-7)*4-1;
    p->NumWords = (sizeof(*p)-sizeof(p->BitPairsMinus1)-sizeof(p->CRC)-sizeof(uint32_t))/sizeof(uint32_t);
    p->CRC = CalcCRC((uint32_t*)&p->Command,(sizeof(*p)-sizeof(p->BitPairsMinus1)-sizeof(p->CRC))/sizeof(uint32_t));
    if (sz) *sz = sizeof(*p)/4;
    return p;
}

void __not_in_flash_func(vmu_handle_block_write)(const uint32_t *pkt, uint32_t num_words) {
    if (num_words < 3) return;
    uint16_t block = (uint16_t)((pkt[1] >> 24) & 0xFF);
    uint8_t  phase = (uint8_t) ((pkt[1] >>  8) & 0xFF);
    if (block >= VMU_TOTAL_BLOCKS || phase >= VMU_PHASE_COUNT_WRITE) return;
    if (block != write_block) { write_block=block; write_phases=0; memset(write_buf,0,VMU_BLOCK_SIZE); }
    uint32_t phase_off = (uint32_t)phase * VMU_WRITE_PHASE_SIZE;
    uint32_t data_bytes = (num_words-2)*4;
    if (data_bytes > VMU_WRITE_PHASE_SIZE) data_bytes = VMU_WRITE_PHASE_SIZE;
    memcpy(write_buf + phase_off, &pkt[2], data_bytes);
    write_phases |= (1u << phase);
    vmu_status = VMU_STATUS_SAVING;
}

void __not_in_flash_func(vmu_handle_write_complete)(void) {
    if (write_block == 0xFFFF || write_phases == 0) return;
    memcpy(vmu_ram + (uint32_t)write_block * VMU_BLOCK_SIZE, write_buf, VMU_BLOCK_SIZE);
    write_block = 0xFFFF;
    write_phases = 0;
    vmu_status = VMU_STATUS_OK;
    // Signal Core 0 to flush to SD — use volatile flag, safe from RAM context
    vmu_dirty_flag = true;
}

void vmu_task(void) {
    // Flush dirty VMU RAM to the active backend when its writeback timer expires
    vmu_storage_task();
}
