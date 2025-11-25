// input_codes.h

#ifndef INPUT_CODES_H
#define INPUT_CODES_H

#include <stdint.h>
#include "tusb.h"
#include "core/globals.h"

// Define constants
#define HOTKEY_LENGTH 10
#ifndef KONAMI_CODE
#define KONAMI_CODE {USBR_BUTTON_DU, USBR_BUTTON_DU, USBR_BUTTON_DD, USBR_BUTTON_DD, USBR_BUTTON_DL, USBR_BUTTON_DR, USBR_BUTTON_DL, USBR_BUTTON_DR, USBR_BUTTON_B1, USBR_BUTTON_B2}
#endif

// Declaration of global variables
extern uint32_t code_buffer[HOTKEY_LENGTH];
extern uint32_t konami_code[HOTKEY_LENGTH];
extern bool is_fun;
extern unsigned char fun_inc;
extern unsigned char fun_player;

// Function declarations
void codes_task(void);
void __not_in_flash_func(shift_buffer_and_insert)(uint32_t new_value);
void __not_in_flash_func(check_for_konami_code)(void);

#endif // INPUT_CODES_H
