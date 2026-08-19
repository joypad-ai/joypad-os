// platform_ch32.c - Platform HAL for WCH CH32V307 (RISC-V, bare metal)
//
// Implements platform.h using the CH32V307 SDK + RISC-V SysTick. Mirrors
// platform_nrf.c / platform_esp32.c but for the WCH bare-metal environment
// (CFG_TUSB_OS == OPT_OS_NONE). Time comes from the free-running 64-bit
// SysTick counter (HCLK rate); identity from the on-chip 96-bit unique ID.

#include "platform/platform.h"
#include <stdio.h>
#include <string.h>

#include "ch32v30x.h"

// CH32V307 factory-programmed unique ID: 96 bits at 0x1FFFF7E8.
#define CH32_UNIQUE_ID_ADDR  0x1FFFF7E8UL

// ============================================================================
// TIME — free-running SysTick (no interrupt), counts up at SystemCoreClock.
// ============================================================================

static void systick_ensure_running(void) {
  static int started = 0;
  if (started) return;
  started = 1;
  // CTLR: STE(bit0)=enable, STCLK(bit2)=HCLK (not HCLK/8). No interrupt, no reload.
  SysTick->CTLR = 0;
  SysTick->CNT  = 0;
  SysTick->CMP  = 0xFFFFFFFFFFFFFFFFULL;
  SysTick->CTLR = (1u << 0) | (1u << 2);
}

uint32_t platform_time_us(void) {
  systick_ensure_running();
  uint32_t per_us = SystemCoreClock / 1000000u;
  if (per_us == 0) per_us = 1;
  return (uint32_t)(SysTick->CNT / per_us);
}

uint32_t platform_time_ms(void) {
  systick_ensure_running();
  uint32_t per_ms = SystemCoreClock / 1000u;
  if (per_ms == 0) per_ms = 1;
  return (uint32_t)(SysTick->CNT / per_ms);
}

void platform_sleep_us(uint32_t us) {
  uint32_t start = platform_time_us();
  while ((uint32_t)(platform_time_us() - start) < us) { /* busy-wait */ }
}

void platform_sleep_ms(uint32_t ms) {
  uint32_t start = platform_time_ms();
  while ((uint32_t)(platform_time_ms() - start) < ms) { /* busy-wait */ }
}

// ============================================================================
// IDENTITY — on-chip 96-bit unique ID
// ============================================================================

void platform_get_unique_id(uint8_t* buf, size_t len) {
  const volatile uint8_t* uid = (const volatile uint8_t*) CH32_UNIQUE_ID_ADDR;
  for (size_t i = 0; i < len; i++) {
    buf[i] = (i < 12) ? uid[i] : 0;
  }
}

void platform_get_serial(char* buf, size_t len) {
  uint8_t id[8];
  platform_get_unique_id(id, sizeof(id));
  size_t pos = 0;
  for (size_t i = 0; i < sizeof(id) && pos + 2 < len; i++) {
    pos += (size_t) snprintf(buf + pos, len - pos, "%02x", id[i]);
  }
  if (len > 0) buf[len - 1] = '\0';
}

// ============================================================================
// REBOOT
// ============================================================================

void platform_reboot(void) {
  NVIC_SystemReset();
  while (1) { }
}

void platform_reboot_bootloader(void) {
  // CH32V307 has no UF2 bootloader; the factory ISP is entered via BOOT0/BOOT1
  // pin strapping, not a software flag. Best effort: plain reset.
  // TODO: jump to system-memory ISP if a software entry path is identified.
  platform_reboot();
}

void platform_reboot_ota(void) {
  // No BLE radio and no OTA bootloader on this part. platform.h specifies that
  // platforms without one fall back to the normal bootloader path, which is
  // what platform_rp2040.c does (reset_usb_boot).
  platform_reboot_bootloader();
}

// ============================================================================
// DIAGNOSTICS
//
// These four are queried by the shared CDC config channel (cdc_commands.c,
// INFO). platform.h defines an explicit "not available here" value for each,
// which is what the RP2040 backend returns as well — so these are honest
// answers to the contract, not placeholders.
// ============================================================================

uint32_t platform_last_reset_reason(void) {
  // 0 = unknown, per platform.h. The CH32V307 does latch reset flags in
  // RCC->RSTSCKR, but nothing here has been able to verify their encoding on
  // real silicon, and a wrong reason is worse than no reason for a field that
  // exists to diagnose unexplained reboots.
  return 0;
}

int platform_battery_millivolts(void) {
  // -1 = no battery-sense circuit, per platform.h. The ch32v307v_r1_1v0 board
  // is USB/bench powered with no divider to an ADC input.
  return -1;
}

int platform_battery_charging(void) {
  // -1 = no charge-status line on this board, per platform.h.
  return -1;
}

// Note: tusb_time_millis_api()/tusb_time_delay_ms_api() (used by the ch32 USB
// drivers) are provided by the TinyUSB board layer (hw/bsp/board.c). A future
// standalone ch32/ build without that board layer must supply them itself.
