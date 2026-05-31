// Joypad OS board header for the Waveshare RP2350B-Plus-W.
//
// Adapts the Raspberry Pi Pico 2 W reference header for the Waveshare board:
//   - RP2350B (48-pin) instead of RP2350A (30-pin) -> drops PICO_RP2350A
//   - 16 MB flash instead of 4 MB
//   - Raspberry Pi RM2 (CYW43) wired to a different set of RP2350 GPIOs
//     (Waveshare picked pins only the B-package exposes; the A-package
//     would not have them broken out at all)
//
// Waveshare RM2 wiring (verified against arduino-pico variant
// waveshare_rp2350b_plus_w/init.cpp):
//   WL_REG_ON   = GP36   (Pico 2 W uses GP23)
//   WL_DATA*    = GP37   (Pico 2 W uses GP24)
//   WL_CS       = GP38   (Pico 2 W uses GP25)
//   WL_CLOCK    = GP39   (Pico 2 W uses GP29)
//
// LED layout: LED1 sits on the RM2 module's CYW43 GPIO0 (same convention as
// the Pico 2 W -- use cyw43_arch_gpio_put(0, ...)). LED2 is on the bare RP2350
// GPIO23 (which was WL_REG_ON on the Pico 2 W -- another reason a stock
// pico2_w build cannot drive the radio on this board: it would just blink an
// LED instead of asserting REG_ON).

#ifndef _BOARDS_WAVESHARE_RP2350B_PLUS_W_H
#define _BOARDS_WAVESHARE_RP2350B_PLUS_W_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)
pico_board_cmake_set(PICO_CYW43_SUPPORTED, 1)

#define WAVESHARE_RP2350B_PLUS_W

// --- UART ---
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

// --- LED ---
// LED1 lives on the RM2's CYW43 GPIO0 (see CYW43_WL_GPIO_LED_PIN below).
// LED2 is on RP2350 GPIO23; expose that as the default LED pin.
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 23
#endif

// --- I2C ---
#ifndef PICO_DEFAULT_I2C
#define PICO_DEFAULT_I2C 0
#endif
#ifndef PICO_DEFAULT_I2C_SDA_PIN
#define PICO_DEFAULT_I2C_SDA_PIN 4
#endif
#ifndef PICO_DEFAULT_I2C_SCL_PIN
#define PICO_DEFAULT_I2C_SCL_PIN 5
#endif

// --- SPI ---
#ifndef PICO_DEFAULT_SPI
#define PICO_DEFAULT_SPI 0
#endif
#ifndef PICO_DEFAULT_SPI_SCK_PIN
#define PICO_DEFAULT_SPI_SCK_PIN 18
#endif
#ifndef PICO_DEFAULT_SPI_TX_PIN
#define PICO_DEFAULT_SPI_TX_PIN 19
#endif
#ifndef PICO_DEFAULT_SPI_RX_PIN
#define PICO_DEFAULT_SPI_RX_PIN 16
#endif
#ifndef PICO_DEFAULT_SPI_CSN_PIN
#define PICO_DEFAULT_SPI_CSN_PIN 17
#endif

// --- FLASH ---
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

// --- CYW43 (RM2) ---
#ifndef CYW43_WL_GPIO_COUNT
#define CYW43_WL_GPIO_COUNT 3
#endif
#ifndef CYW43_WL_GPIO_LED_PIN
#define CYW43_WL_GPIO_LED_PIN 0    // LED1, on the RM2 module
#endif
#ifndef CYW43_WL_GPIO_SMPS_PIN
#define CYW43_WL_GPIO_SMPS_PIN 1
#endif
#ifndef CYW43_WL_GPIO_VBUS_PIN
#define CYW43_WL_GPIO_VBUS_PIN 2
#endif
#ifndef CYW43_USES_VSYS_PIN
#define CYW43_USES_VSYS_PIN 1
#endif

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

// RM2 SPI pins -- Waveshare chose pins only the RP2350B package exposes.
#ifndef CYW43_PIN_WL_DYNAMIC
#define CYW43_PIN_WL_DYNAMIC 0
#endif
#ifndef CYW43_DEFAULT_PIN_WL_REG_ON
#define CYW43_DEFAULT_PIN_WL_REG_ON 36u
#endif
#ifndef CYW43_DEFAULT_PIN_WL_DATA_OUT
#define CYW43_DEFAULT_PIN_WL_DATA_OUT 37u
#endif
#ifndef CYW43_DEFAULT_PIN_WL_DATA_IN
#define CYW43_DEFAULT_PIN_WL_DATA_IN 37u
#endif
#ifndef CYW43_DEFAULT_PIN_WL_HOST_WAKE
#define CYW43_DEFAULT_PIN_WL_HOST_WAKE 37u
#endif
#ifndef CYW43_DEFAULT_PIN_WL_CLOCK
#define CYW43_DEFAULT_PIN_WL_CLOCK 39u
#endif
#ifndef CYW43_DEFAULT_PIN_WL_CS
#define CYW43_DEFAULT_PIN_WL_CS 38u
#endif

#endif
