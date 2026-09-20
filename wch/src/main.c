/*
 * Joypad OS — usb2usb on WCH CH32V307 (RISC-V, bare metal).
 *
 *   USBFS (PA11/PA12) = USB host input  → Joypad usbh registry → router
 *   USBHS (PB6/PB7)   = USB device out  ← Joypad usbd output modes
 *
 * Single-core equivalent of src/main.c (the RP2040 entry). The TinyUSB host
 * (USBFS) and device (USBHS) stacks are each brought up by their owning Joypad
 * interface (usbh_init / usbd_init call tusb_init for their rhport). Mirrors the
 * esp/ and nrf/ ports: board_init → shared services → app → interface init →
 * cooperative main loop.
 */

#include <stdio.h>

#include "bsp/board_api.h"
#include "tusb.h"

#include "platform/platform.h"
#include "core/app_registry.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "core/services/players/manager.h"
#include "core/services/leds/leds.h"
#include "core/services/storage/storage.h"

// App layer (apps/usb2usb/app.c)
extern void app_init(void);
extern void app_task(void);
extern const OutputInterface** app_get_output_interfaces(uint8_t* count);
extern const InputInterface** app_get_input_interfaces(uint8_t* count);

static const OutputInterface** outputs = NULL;
static uint8_t output_count = 0;
static const InputInterface** inputs = NULL;
static uint8_t input_count = 0;

// Referenced by shared code (CDC config, web config) — main owns these, as on RP2040.
const OutputInterface* active_output = NULL;
const OutputInterface* native_output = NULL;

int main(void)
{
  board_init();   // CH32 BSP: clocks (HS PLL), USART1 debug @115200, LED

  printf("\n[joypad] CH32V307 usb2usb v%s\n", JOYPAD_VERSION);
  printf("[joypad]   host=USBFS(PA11/PA12)  device=USBHS(PB6/PB7)\n");

  // Shared services
  leds_init();
  storage_init();
  players_init();
  app_init();

  // Output interfaces (usbd_init brings up the USBHS device stack on rhport 0)
  outputs = app_get_output_interfaces(&output_count);
  if (output_count > 0 && outputs[0]) {
    active_output = outputs[0];
  }
  for (uint8_t i = 0; i < output_count; i++) {
    if (outputs[i] && outputs[i]->init) {
      printf("[joypad] Initializing output: %s\n", outputs[i]->name);
      outputs[i]->init();
    }
  }

  // Input interfaces (usbh_init brings up the USBFS host stack on rhport 1)
  inputs = app_get_input_interfaces(&input_count);
  for (uint8_t i = 0; i < input_count; i++) {
    if (inputs[i] && inputs[i]->init) {
      printf("[joypad] Initializing input: %s\n", inputs[i]->name);
      inputs[i]->init();
    }
  }

  // Publish active interfaces so shared code (CDC, router) can introspect.
  app_registry_set(inputs, input_count, outputs, output_count);

  printf("[joypad] Entering main loop\n");

  while (1) {
    // CH32 USBFS host attach/disconnect is now self-driven from the SOF ISR
    // (hcd_ch32_usbfs.c); no per-loop host poll is required.
    leds_task();
    players_task();
    storage_task();

    // Poll inputs first so outputs read the freshest router state this iteration
    for (uint8_t i = 0; i < input_count; i++) {
      if (inputs[i] && inputs[i]->task) inputs[i]->task();
    }
    for (uint8_t i = 0; i < output_count; i++) {
      if (outputs[i] && outputs[i]->task) outputs[i]->task();
    }

    app_task();
  }

  return 0;
}
