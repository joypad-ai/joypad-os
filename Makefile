# Joypad Top-Level Makefile
# Builds firmware for all product variants

# Load local environment overrides (gitignored)
-include .env
export

# Default target
.DEFAULT_GOAL := help

# Parallel jobs for cmake builds (auto-detect cores, fallback to 4)
JOBS := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Ensure PICO_TOOLCHAIN_PATH is set. Prefer the latest ARM GNU Toolchain
# installed under /Applications/ArmGNUToolchain/ (the cask `gcc-arm-embedded`
# .pkg, which bundles newlib). Fall back to PATH for Linux/CI/Docker.
# Note: the brew formula `arm-none-eabi-gcc` is NOT a viable substitute on
# macOS — it lacks newlib (`nosys.specs` missing) and bare-metal links fail.
# Docker pins ARM 15.2.rel1 (see Dockerfile); 14.x locally is fine for
# matching codegen on the d6c02ac pico-pio-usb pin.
ifndef PICO_TOOLCHAIN_PATH
    TOOLCHAIN_PATH_MACOS := $(shell ls -d /Applications/ArmGNUToolchain/*/arm-none-eabi 2>/dev/null | sort -V | tail -1)
    TOOLCHAIN_IN_PATH := $(shell which arm-none-eabi-gcc 2>/dev/null)

    ifneq ($(TOOLCHAIN_PATH_MACOS),)
        export PICO_TOOLCHAIN_PATH := $(TOOLCHAIN_PATH_MACOS)
    else ifneq ($(TOOLCHAIN_IN_PATH),)
        export PICO_TOOLCHAIN_PATH :=
    else
        $(error No ARM toolchain found. Install with `brew install --cask gcc-arm-embedded` then run the .pkg installer (puts toolchain at /Applications/ArmGNUToolchain/X.Y.relZ/))
    endif
endif

# Use local pico-sdk submodule by default
ifndef PICO_SDK_PATH
    export PICO_SDK_PATH := $(CURDIR)/src/lib/pico-sdk
else
    # If PICO_SDK_PATH is set but doesn't exist, use submodule instead
    ifeq ($(wildcard $(PICO_SDK_PATH)),)
        $(warning PICO_SDK_PATH is set to '$(PICO_SDK_PATH)' but directory not found, using submodule instead)
        override PICO_SDK_PATH := $(CURDIR)/src/lib/pico-sdk
    endif
endif

# Use external TinyUSB (keeps pico-sdk clean)
export PICO_TINYUSB_PATH := $(CURDIR)/src/lib/tinyusb

# Board-specific build scripts
BOARD_SCRIPT_pico := boards/build_rpi_pico.sh
BOARD_SCRIPT_pico_w := boards/build_pico_w.sh
BOARD_SCRIPT_pico2 := boards/build_pico2.sh
BOARD_SCRIPT_pico2_w := boards/build_pico2_w.sh
BOARD_SCRIPT_kb2040 := boards/build_ada_kb2040.sh
BOARD_SCRIPT_qtpy := boards/build_ada_qtpy.sh
BOARD_SCRIPT_rp2040zero := boards/build_waveshare_rp2040_zero.sh
BOARD_SCRIPT_feather_usbhost := boards/build_ada_feather_usbhost.sh
BOARD_SCRIPT_feather := boards/build_ada_feather.sh
BOARD_SCRIPT_macropad := boards/build_ada_macropad.sh
BOARD_SCRIPT_rp2350usba := boards/build_waveshare_rp2350_usb_a.sh

# Console targets (cmake target names)
CONSOLE_3do := joypad_3do
CONSOLE_pce := joypad_pce
CONSOLE_ngc := joypad_ngc
CONSOLE_ngc_rp2040zero := joypad_ngc_rp2040zero
CONSOLE_nuon := joypad_nuon
CONSOLE_nuonserial := joypad_nuonserial
CONSOLE_loopy := joypad_loopy
CONSOLE_dc := joypad_dc
CONSOLE_dc_rp2040zero := joypad_dc_rp2040zero
CONSOLE_usb_pico := joypad_usb_pico
CONSOLE_usb_pico_w := joypad_usb_pico_w
CONSOLE_usb_pico2_w := joypad_usb_pico2_w
CONSOLE_neogeo := joypad_neogeo
CONSOLE_neogeo_pico := joypad_neogeo_pico
CONSOLE_neogeo_rp2040zero := joypad_neogeo_rp2040zero
CONSOLE_n642dc := joypad_n642dc
CONSOLE_n642dc_pico2_w := joypad_n642dc_pico2_w
CONSOLE_n642nuon := joypad_n642nuon
CONSOLE_n642nuon_pico := joypad_n642nuon
CONSOLE_n642nuon_aries64 := joypad_n642nuon_aries64
CONSOLE_snes3do := joypad_snes3do
CONSOLE_uart := joypad_uart
CONSOLE_usb_feather_rp2040 := joypad_usb_feather_rp2040
CONSOLE_usb_feather_rp2040_usb_host := joypad_usb_feather_rp2040_usb_host
CONSOLE_usb_feather_rp2040_max3421 := joypad_usb_feather_rp2040_max3421
CONSOLE_usb_feather_rp2040_usb_host_max3421 := joypad_usb_feather_rp2040_usb_host_max3421
CONSOLE_usb_rp2040zero := joypad_usb_rp2040zero
CONSOLE_usb_rp2350usba := joypad_usb_rp2350usba
CONSOLE_bt2usb := joypad_bt2usb
CONSOLE_bt2loopy := joypad_bt2loopy
CONSOLE_bt2nuon := joypad_bt2nuon
CONSOLE_bt2n64 := joypad_bt2n64
CONSOLE_btusb2usb := joypad_btusb2usb
CONSOLE_usb2ble := joypad_usb2ble
CONSOLE_n64 := joypad_n64
CONSOLE_wifi2usb := joypad_wifi2usb
CONSOLE_snes2usb := joypad_snes2usb
CONSOLE_nes2usb := joypad_nes2usb
CONSOLE_n642usb := joypad_n642usb
CONSOLE_nuon2usb := joypad_nuon2usb
CONSOLE_gc2usb := joypad_gc2usb
CONSOLE_gc2usb_pico := joypad_gc2usb_pico
CONSOLE_wii2usb := joypad_wii2usb
CONSOLE_wii2gc := joypad_wii2gc
CONSOLE_wii2n64 := joypad_wii2n64
CONSOLE_lodgenet2usb := joypad_lodgenet2usb
CONSOLE_lodgenet2n64 := joypad_lodgenet2n64
CONSOLE_lodgenet2gc := joypad_lodgenet2gc
CONSOLE_neogeo2usb := joypad_neogeo2usb
CONSOLE_neogeo2usb_rp2040zero := joypad_neogeo2usb_rp2040zero
CONSOLE_jvs2usb_rp2040zero := joypad_jvs2usb_rp2040zero
CONSOLE_controller_fisherprice_v1 := joypad_controller_fisherprice_v1
CONSOLE_controller_fisherprice_v2 := joypad_controller_fisherprice_v2
CONSOLE_controller_alpakka := joypad_controller_alpakka
CONSOLE_controller_macropad := joypad_controller_macropad
CONSOLE_bt2gc := joypad_bt2gc
CONSOLE_bt2wiiext := joypad_bt2wiiext
CONSOLE_controller_btusb := joypad_controller_btusb
CONSOLE_controller_btusb_rp2040_abb := joypad_controller_btusb_rp2040_abb
CONSOLE_controller_btusb_feather_rp2040 := joypad_controller_btusb_feather_rp2040
CONSOLE_controller_btusb_feather_rp2040_usb_host := joypad_controller_btusb_feather_rp2040_usb_host
CONSOLE_controller_btusb_fisherprice_v1 := joypad_controller_btusb_fisherprice_v1
CONSOLE_controller_btusb_fisherprice_v2 := joypad_controller_btusb_fisherprice_v2
CONSOLE_controller_btusb_alpakka := joypad_controller_btusb_alpakka


# App definitions: APP_name = board target output_name input output
# Naming convention: <app>_<board> for all apps
APP_usb2pce_kb2040 := kb2040 pce usb2pce_kb2040 USB/BT PCEngine
APP_usb2gc_kb2040 := kb2040 ngc usb2gc_kb2040 USB/BT GameCube
APP_usb2gc_rp2040zero := rp2040zero ngc_rp2040zero usb2gc_rp2040zero USB/BT GameCube
APP_usb2nuon_kb2040 := kb2040 nuon usb2nuon_kb2040 USB/BT Nuon
APP_nuonserial_kb2040 := kb2040 nuonserial nuonserial_kb2040 Nuon CDC-Serial
APP_usb2loopy_kb2040 := kb2040 loopy usb2loopy_kb2040 USB/BT Loopy
APP_usb2dc_kb2040 := kb2040 dc usb2dc_kb2040 USB/BT Dreamcast
APP_usb2dc_rp2040zero := rp2040zero dc_rp2040zero usb2dc_rp2040zero USB/BT Dreamcast
APP_usb2neogeo_kb2040 := kb2040 neogeo usb2neogeo_kb2040 USB/BT NEOGEO
APP_usb2neogeo_pico := pico neogeo_pico usb2neogeo_pico USB/BT NEOGEO
APP_usb2neogeo_rp2040zero := rp2040zero neogeo_rp2040zero usb2neogeo_rp2040zero USB/BT NEOGEO
APP_n642dc_kb2040 := kb2040 n642dc n642dc_kb2040 N64 Dreamcast
APP_n642dc_pico2_w := pico2_w n642dc_pico2_w n642dc_pico2_w N64 Dreamcast
APP_nes2usb_kb2040 := kb2040 nes2usb nes2usb_kb2040 NES USB
APP_nes2usb_pico_w := pico_w nes2usb nes2usb_pico_w NES USB
APP_n642nuon_pico := pico n642nuon n642nuon_pico N64 Nuon
APP_n642nuon_aries64 := pico n642nuon_aries64 n642nuon_aries64 N64 Nuon
APP_usb23do_rp2040zero := rp2040zero 3do usb23do_rp2040zero USB/BT 3DO
APP_snes23do_rp2040zero := rp2040zero snes3do snes23do_rp2040zero SNES 3DO
APP_usb2uart_kb2040 := kb2040 uart usb2uart_kb2040 USB/BT UART
APP_usb2usb_pico := pico usb_pico usb2usb_pico USB/BT USB
APP_usb2usb_pico_w := pico_w usb_pico_w usb2usb_pico_w USB/BT USB
APP_usb2usb_pico2_w := pico2_w usb_pico2_w usb2usb_pico2_w USB/BT USB
APP_usb2usb_feather_rp2040 := feather usb_feather_rp2040 usb2usb_feather_rp2040 USB/BT USB
APP_usb2usb_feather_rp2040_usb_host := feather_usbhost usb_feather_rp2040_usb_host usb2usb_feather_rp2040_usb_host USB/BT USB
APP_usb2usb_feather_rp2040_max3421 := feather usb_feather_rp2040_max3421 usb2usb_feather_rp2040_max3421 USB/BT USB
APP_usb2usb_feather_rp2040_usb_host_max3421 := feather_usbhost usb_feather_rp2040_usb_host_max3421 usb2usb_feather_rp2040_usb_host_max3421 USB/BT USB
APP_usb2usb_rp2040zero := rp2040zero usb_rp2040zero usb2usb_rp2040zero USB/BT USB
APP_usb2usb_rp2350usba := rp2350usba usb_rp2350usba usb2usb_rp2350usba USB/BT USB
APP_bt2usb_pico_w := pico_w bt2usb bt2usb_pico_w Bluetooth USB
APP_bt2usb_pico2_w := pico2_w bt2usb bt2usb_pico2_w Bluetooth USB
APP_bt2loopy_pico_w := pico_w bt2loopy bt2loopy_pico_w Bluetooth Loopy
APP_bt2nuon_pico_w := pico_w bt2nuon bt2nuon_pico_w Bluetooth Nuon
APP_bt2nuon_pico2_w := pico2_w bt2nuon bt2nuon_pico2_w Bluetooth Nuon
APP_bt2n64_pico_w := pico_w bt2n64 bt2n64_pico_w Bluetooth N64
APP_bt2n64_pico2_w := pico2_w bt2n64 bt2n64_pico2_w Bluetooth N64
APP_bt2gc_pico_w := pico_w bt2gc bt2gc_pico_w Bluetooth GameCube
APP_bt2gc_pico2_w := pico2_w bt2gc bt2gc_pico2_w Bluetooth GameCube
APP_bt2wiiext_pico_w := pico_w bt2wiiext bt2wiiext_pico_w Bluetooth Wii extension
APP_btusb2usb_pico_w := pico_w btusb2usb btusb2usb_pico_w USB/BT+CYW43 USB
APP_btusb2usb_pico2_w := pico2_w btusb2usb btusb2usb_pico2_w USB/BT+CYW43 USB
APP_usb2ble_pico_w := pico_w usb2ble usb2ble_pico_w USB BLE
APP_usb2ble_pico2_w := pico2_w usb2ble usb2ble_pico2_w USB BLE
APP_usb2n64_kb2040 := kb2040 n64 usb2n64_kb2040 USB/BT N64
APP_wifi2usb_pico_w := pico_w wifi2usb wifi2usb_pico_w WiFi USB
APP_wifi2usb_pico2_w := pico2_w wifi2usb wifi2usb_pico2_w WiFi USB
APP_snes2usb_kb2040 := kb2040 snes2usb snes2usb_kb2040 SNES USB
APP_n642usb_kb2040 := kb2040 n642usb n642usb_kb2040 N64 USB
APP_nuon2usb_kb2040 := kb2040 nuon2usb nuon2usb_kb2040 Nuon USB
APP_nuon2usb_pico_w := pico_w nuon2usb nuon2usb_pico_w Nuon USB
APP_gc2usb_kb2040 := kb2040 gc2usb gc2usb_kb2040 GameCube USB
APP_gc2usb_rp2040zero := rp2040zero gc2usb gc2usb_rp2040zero GameCube USB
APP_gc2usb_pico := pico gc2usb_pico gc2usb_pico GameCube USB
APP_wii2usb_kb2040 := kb2040 wii2usb wii2usb_kb2040 Wii USB
APP_wii2gc_kb2040 := kb2040 wii2gc wii2gc_kb2040 Wii GameCube
APP_wii2n64_pico := pico wii2n64 wii2n64_pico Wii N64
APP_lodgenet2usb_pico := pico lodgenet2usb lodgenet2usb_pico LodgeNet USB
APP_lodgenet2usb_pico2 := pico2 lodgenet2usb lodgenet2usb_pico2 LodgeNet USB
APP_lodgenet2n64_pico := pico lodgenet2n64 lodgenet2n64_pico LodgeNet N64
APP_lodgenet2gc_pico := pico lodgenet2gc lodgenet2gc_pico LodgeNet GameCube
APP_neogeo2usb_kb2040 := kb2040 neogeo2usb neogeo2usb_kb2040 NEOGEO USB
APP_neogeo2usb_rp2040zero := rp2040zero neogeo2usb_rp2040zero neogeo2usb_rp2040zero NEOGEO USB
APP_jvs2usb_rp2040zero := rp2040zero jvs2usb_rp2040zero jvs2usb_rp2040zero JVS USB
APP_controller_fisherprice_v1_kb2040 := kb2040 controller_fisherprice_v1 controller_fisherprice_v1_kb2040 GPIO USB
APP_controller_fisherprice_v2_kb2040 := kb2040 controller_fisherprice_v2 controller_fisherprice_v2_kb2040 GPIO/ADC USB
APP_controller_alpakka_pico := pico controller_alpakka controller_alpakka_pico GPIO/I2C USB
APP_controller_macropad := macropad controller_macropad controller_macropad GPIO USB
APP_controller_btusb_fisherprice_v1_kb2040 := kb2040 controller_btusb_fisherprice_v1 controller_btusb_fisherprice_v1_kb2040 GPIO USB
APP_controller_btusb_fisherprice_v2_kb2040 := kb2040 controller_btusb_fisherprice_v2 controller_btusb_fisherprice_v2_kb2040 GPIO/ADC USB
APP_controller_btusb_alpakka_pico := pico controller_btusb_alpakka controller_btusb_alpakka_pico GPIO/I2C USB
APP_controller_btusb_pico_w := pico_w controller_btusb controller_btusb_pico_w JoyWing BLE/USB
APP_controller_btusb_pico2_w := pico2_w controller_btusb controller_btusb_pico2_w JoyWing BLE/USB
APP_controller_btusb_rp2040_abb := pico controller_btusb_rp2040_abb controller_btusb_rp2040_abb ABB USB
APP_controller_btusb_feather_rp2040 := feather controller_btusb_feather_rp2040 controller_btusb_feather_rp2040 JoyWing USB
APP_controller_btusb_feather_rp2040_usb_host := feather_usbhost controller_btusb_feather_rp2040_usb_host controller_btusb_feather_rp2040_usb_host JoyWing USB


# All apps (note: controller_macropad not included - build explicitly with 'make controller_macropad')
# Note: usb2loopy_kb2040, snes23do_rp2040zero excluded until more mature
APPS := usb2pce_kb2040 usb2gc_kb2040 usb2gc_rp2040zero usb2nuon_kb2040 usb2n64_kb2040 usb2dc_kb2040 usb2dc_rp2040zero usb2neogeo_kb2040 usb2neogeo_pico usb2neogeo_rp2040zero n642dc_kb2040 n642dc_pico2_w n642nuon_pico usb23do_rp2040zero usb2uart_kb2040 usb2usb_pico usb2usb_pico_w usb2usb_pico2_w usb2usb_feather_rp2040 usb2usb_feather_rp2040_usb_host usb2usb_feather_rp2040_max3421 usb2usb_feather_rp2040_usb_host_max3421 usb2usb_rp2040zero usb2usb_rp2350usba bt2usb_pico_w bt2usb_pico2_w btusb2usb_pico_w btusb2usb_pico2_w usb2ble_pico_w usb2ble_pico2_w bt2nuon_pico_w bt2nuon_pico2_w bt2n64_pico_w bt2n64_pico2_w snes2usb_kb2040 n642usb_kb2040 gc2usb_kb2040 gc2usb_rp2040zero nes2usb_kb2040 nes2usb_pico_w controller_fisherprice_v1_kb2040 controller_fisherprice_v2_kb2040 controller_alpakka_pico

# Stable apps for release
# Note: usb2loopy_kb2040, snes23do_rp2040zero excluded until more mature
RELEASE_APPS := usb2pce_kb2040 usb2gc_kb2040 usb2gc_rp2040zero usb2nuon_kb2040 usb23do_rp2040zero usb2usb_feather_rp2040 usb2usb_feather_rp2040_usb_host usb2usb_rp2040zero bt2usb_pico_w snes2usb_kb2040

# Release directory
RELEASE_DIR := releases

# Get git commit hash (short, 7 chars)
# Can be overridden via environment variable for Docker/CI builds
GIT_COMMIT ?= $(shell git rev-parse --short=7 HEAD 2>/dev/null || echo "unknown")

# Version identifier (use VERSION file if RELEASE_VERSION is set, otherwise commit hash)
ifdef RELEASE_VERSION
    VERSION_ID := $(RELEASE_VERSION)
else
    VERSION_ID := $(GIT_COMMIT)
endif

# ANSI color codes
GREEN := \033[0;32m
YELLOW := \033[1;33m
BLUE := \033[0;34m
NC := \033[0m

# Help target
.PHONY: help
help:
	@echo ""
	@echo "$(BLUE)Joypad Firmware Build System$(NC)"
	@echo "$(BLUE)==============================$(NC)"
	@echo ""
	@echo "$(GREEN)Quick Start:$(NC)"
	@echo "  make init          - Initialize submodules (run once after clone)"
	@echo "  make init-esp      - Install ESP-IDF for ESP32-S3 builds"
	@echo "  make build         - Build all apps (alias for 'make all')"
	@echo ""
	@echo "$(GREEN)App Targets:$(NC)"
	@echo "  make usb2pce_kb2040     - USB/BT -> PCEngine (KB2040)"
	@echo "  make usb2gc_kb2040      - USB/BT -> GameCube (KB2040)"
	@echo "  make usb2gc_rp2040zero  - USB/BT -> GameCube (RP2040-Zero)"
	@echo "  make usb2nuon_kb2040    - USB/BT -> Nuon (KB2040)"
	@echo "  make usb2n64_kb2040     - USB/BT -> N64 (KB2040)"
	@echo "  make usb2loopy_kb2040   - USB/BT -> Loopy (KB2040)"
	@echo "  make usb2dc_kb2040      - USB/BT -> Dreamcast (KB2040)"
	@echo "  make usb2dc_rp2040zero  - USB/BT -> Dreamcast (RP2040-Zero, USB4Maple-compatible)"
	@echo "  make usb2neogeo_kb2040  - USB/BT -> NEOGEO (KB2040)"
	@echo "  make usb2neogeo_pico    - USB/BT -> NEOGEO (Pi Pico)"
	@echo "  make usb2neogeo_rp2040zero - USB/BT -> NEOGEO (RP2040-Zero)"
	@echo "  make n642dc_kb2040      - N64 -> Dreamcast (KB2040)"
	@echo "  make n642dc_pico2_w     - N64 -> Dreamcast (Pi Pico 2 W)"
	@echo "  make n642nuon_pico    - N64 -> Nuon (KB2040)"
	@echo "  make usb23do_rp2040zero - USB/BT -> 3DO (RP2040-Zero)"
	@echo "  make snes23do_rp2040zero - SNES -> 3DO (RP2040-Zero)"
	@echo "  make usb2uart_kb2040    - USB -> UART/ESP32 (KB2040)"
	@echo "  make usb2usb_pico       - USB/BT -> USB HID (Pi Pico)"
	@echo "  make usb2usb_pico_w     - USB/BT -> USB HID (Pi Pico W)"
	@echo "  make usb2usb_pico2_w    - USB/BT -> USB HID (Pi Pico 2 W)"
	@echo "  make usb2usb_feather_rp2040 - USB/BT -> USB HID (Feather RP2040, PIO-USB)"
	@echo "  make usb2usb_feather_rp2040_usb_host - USB/BT -> USB HID (Feather RP2040 USB Host, PIO-USB)"
	@echo "  make usb2usb_feather_rp2040_max3421 - USB/BT -> USB HID (Feather RP2040 + MAX3421E FeatherWing)"
	@echo "  make usb2usb_feather_rp2040_usb_host_max3421 - USB/BT -> USB HID (Feather RP2040 USB Host + MAX3421E FeatherWing)"
	@echo "  make usb2usb_rp2040zero - USB/BT -> USB HID (RP2040-Zero)"
	@echo "  make usb2usb_rp2350usba - USB/BT -> USB HID (Waveshare RP2350A)"
	@echo "  make bt2usb_pico_w      - Bluetooth -> USB HID (Pico W)"
	@echo "  make btusb2usb_pico_w   - USB/BT+CYW43 -> USB HID (Pico W, USB host + built-in BT)"
	@echo "  make btusb2usb_pico2_w  - USB/BT+CYW43 -> USB HID (Pico 2 W, USB host + built-in BT)"
	@echo "  make usb2ble_pico_w    - USB -> BLE Gamepad (Pico W, USB host + BLE peripheral)"
	@echo "  make usb2ble_pico2_w   - USB -> BLE Gamepad (Pico 2 W, USB host + BLE peripheral)"
	@echo "  make usb2usb_feather_esp32s3 - USB -> USB HID (Feather ESP32-S3 + MAX3421E FeatherWing)"
	@echo "  make btusb2usb_feather_esp32s3 - USB/BLE -> USB HID (Feather ESP32-S3 + MAX3421E + BLE)"
	@echo "  make bt2usb_xiao_esp32s3     - Bluetooth -> USB HID (ESP32-S3, requires ESP-IDF)"
	@echo "  make uf2-bt2usb_xiao_esp32s3       - Build + generate .uf2 for drag-and-drop update"
	@echo "  make flash-uf2-bt2usb_xiao_esp32s3 - Build + flash .uf2 via TinyUF2 drive"
	@echo "  make bt2usb_seeed_xiao_nrf52840    - Bluetooth -> USB HID (Seeed XIAO nRF52840, requires NCS)"
	@echo "  make flash-bt2usb_seeed_xiao_nrf52840 - Flash Seeed XIAO nRF52840 via UF2 bootloader"
	@echo "  make bt2usb_feather_nrf52840       - Bluetooth -> USB HID (Adafruit Feather nRF52840, requires NCS)"
	@echo "  make flash-bt2usb_feather_nrf52840 - Flash Feather nRF52840 via UF2 bootloader"
	@echo "  make usb2usb_feather_nrf52840 - USB -> USB HID (Feather nRF52840 + MAX3421E FeatherWing)"
	@echo "  make flash-usb2usb_feather_nrf52840 - Flash Feather nRF52840 + MAX3421E via UF2"
	@echo "  make btusb2usb_feather_nrf52840 - USB/BT -> USB HID (Feather nRF52840 + MAX3421E + BLE)"
	@echo "  make flash-btusb2usb_feather_nrf52840 - Flash Feather nRF52840 btusb2usb via UF2"
	@echo "  make controller_btusb_feather_nrf52840 - Sensor/BLE -> USB HID (Feather nRF52840 + JoyWing)"
	@echo "  make flash-controller_btusb_feather_nrf52840 - Flash Feather nRF52840 controller_btusb via UF2"
	@echo "  make bt2loopy_pico_w    - Bluetooth -> Loopy (Pico W)"
	@echo "  make bt2nuon_pico_w     - Bluetooth -> Nuon (Pico W)"
	@echo "  make bt2n64_pico_w      - Bluetooth -> N64 (Pico W)"
	@echo "  make wifi2usb_pico_w    - WiFi -> USB HID (Pico W)"
	@echo "  make snes2usb_kb2040    - SNES -> USB HID (KB2040)"
	@echo "  make n642usb_kb2040     - N64 -> USB HID (KB2040)"
	@echo "  make nuon2usb_kb2040    - Nuon -> USB HID (KB2040)"
	@echo "  make gc2usb_kb2040      - GameCube -> USB HID (KB2040)"
	@echo "  make gc2usb_rp2040zero  - GameCube -> USB HID (RP2040-Zero)"
	@echo "  make neogeo2usb_kb2040  - NEOGEO -> USB HID (KB2040)"
	@echo "  make neogeo2usb_rp2040zero - NEOGEO -> USB HID (RP2040-Zero)"
	@echo "  make jvs2usb_rp2040zero - JVS -> USB HID (RP2040-Zero)"
	@echo "  make controller_fisherprice_v1_kb2040 - Fisher Price V1 (button-only) -> USB HID (KB2040)"
	@echo "  make controller_fisherprice_v2_kb2040 - Fisher Price V2 (analog+shoulders) -> USB HID (KB2040)"
	@echo "  make controller_alpakka_pico - GPIO/I2C -> USB HID (Pico)"
	@echo "  make controller_macropad - 12 keys -> USB HID (MacroPad RP2040)"
	@echo "  make controller_btusb_pico_w - GPIO+JoyWing -> BLE+USB HID (Pico W)"
	@echo "  make controller_btusb_rp2040_abb - GPIO+USB Host -> USB HID (ABB Passthrough)"

	@echo "  make nes2usb_kb2040     - NES -> USB HID (KB2040)"
	@echo "  make nes2usb_pico_w     - NES -> USB HID (Pico W)"
	@echo "  make lodgenet2usb_pico   - LodgeNet -> USB HID (Pico)"
	@echo "  make lodgenet2usb_pico2  - LodgeNet -> USB HID (Pico 2)"
	@echo "  make lodgenet2n64_pico   - LodgeNet -> N64 (Pico)"
	@echo "  make lodgenet2gc_pico    - LodgeNet -> GameCube (Pico)"
	@echo ""
	@echo "$(GREEN)Convenience Targets:$(NC)"
	@echo "  make all           - Build all apps"
	@echo "  make clean         - Clean build artifacts"
	@echo "  make fullclean     - Reset to fresh clone state (removes all untracked files)"
	@echo "  make releases      - Build stable apps for release"
	@echo ""
	@echo "$(GREEN)Flash Targets:$(NC)"
	@echo "  make flash                - Flash most recently built firmware"
	@echo "  make flash-usb2pce_kb2040 - Flash usb2pce_kb2040"
	@echo "  make flash-usb2gc_kb2040  - Flash usb2gc_kb2040"
	@echo "  make flash-usb2gc_rp2040zero - Flash usb2gc_rp2040zero"
	@echo "  (and similar for other apps)"
	@echo ""
	@echo "$(GREEN)Console-Only Targets (uses KB2040):$(NC)"
	@echo "  make pce           - Build PCEngine firmware"
	@echo "  make ngc           - Build GameCube firmware"
	@echo "  make nuon          - Build Nuon firmware"
	@echo "  make loopy         - Build Loopy firmware"
	@echo "  make 3do           - Build 3DO firmware"
	@echo ""
	@echo "$(GREEN)Environment:$(NC)"
	@echo "  PICO_SDK_PATH:       $(PICO_SDK_PATH)"
	@echo "  PICO_TOOLCHAIN_PATH: $(PICO_TOOLCHAIN_PATH)"
	@echo ""

# Initialize submodules (run once after cloning)
.PHONY: init
init:
	@echo "$(YELLOW)Initializing submodules...$(NC)"
	@git submodule update --init --recursive
	@echo "$(YELLOW)Checking out pico-sdk 2.2.0...$(NC)"
	@cd src/lib/pico-sdk && git checkout 2.2.0
	@echo "$(YELLOW)Checking out TinyUSB 0.19.0...$(NC)"
	@cd src/lib/tinyusb && git fetch --tags && git checkout 0.19.0
	@echo "$(GREEN)✓ Initialization complete!$(NC)"
	@echo "$(GREEN)  You can now run 'make build' or 'make all'$(NC)"
	@echo ""

# Initialize ESP-IDF for ESP32-S3 builds
.PHONY: init-esp
init-esp:
	@echo "$(YELLOW)Setting up ESP-IDF for ESP32-S3...$(NC)"
	@if [ ! -d "$(HOME)/esp-idf" ]; then \
		echo "$(YELLOW)Cloning ESP-IDF v6.0...$(NC)"; \
		git clone --branch v6.0 --depth 1 --recursive https://github.com/espressif/esp-idf.git $(HOME)/esp-idf; \
	else \
		echo "$(GREEN)  ESP-IDF already installed at ~/esp-idf$(NC)"; \
	fi
	@echo "$(YELLOW)Installing ESP-IDF tools for ESP32-S3...$(NC)"
	@cd $(HOME)/esp-idf && ./install.sh esp32s3
	@echo "$(YELLOW)Setting up Python environment...$(NC)"
	@bash -c 'source $(HOME)/esp-idf/export.sh && echo "$(GREEN)  Python env: $$IDF_PYTHON_ENV_PATH$(NC)"'
	@echo "$(GREEN)✓ ESP-IDF setup complete!$(NC)"
	@echo "$(GREEN)  You can now run 'make bt2usb_xiao_esp32s3'$(NC)"
	@echo ""

# Initialize nRF Connect SDK for nRF52840 builds
.PHONY: init-nrf
init-nrf:
	@echo "$(YELLOW)Setting up nRF Connect SDK for nRF52840...$(NC)"
	@cd nrf && $(MAKE) init
	@echo "$(GREEN)✓ nRF Connect SDK setup complete!$(NC)"
	@echo "$(GREEN)  You can now run 'make bt2usb_seeed_xiao_nrf52840'$(NC)"
	@echo ""

# Alias for all
.PHONY: build
build: all

# Generic app build function
# Output naming: joypad_<version|commit>_<app>.uf2
define build_app
	@echo "$(YELLOW)Building $1...$(NC)"
	@echo "  Board:   $(word 1,$(APP_$1))"
	@echo "  Input:   $(word 4,$(APP_$1))"
	@echo "  Output:  $(word 5,$(APP_$1))"
	@echo "  Version: $(VERSION_ID)"
	@cd src && rm -rf build
	@cd src && sh $(BOARD_SCRIPT_$(word 1,$(APP_$1)))
	@cd src/build && $(MAKE) --no-print-directory $(CONSOLE_$(word 2,$(APP_$1))) -j$(JOBS)
	@mkdir -p $(RELEASE_DIR)
	@cp src/build/$(CONSOLE_$(word 2,$(APP_$1))).uf2 \
	    $(RELEASE_DIR)/joypad_$(VERSION_ID)_$(word 3,$(APP_$1)).uf2
	@echo "$(GREEN)✓ $1 built successfully$(NC)"
	@echo "  File: $(RELEASE_DIR)/joypad_$(VERSION_ID)_$(word 3,$(APP_$1)).uf2"
	@echo ""
endef

# App targets
.PHONY: usb2pce_kb2040
usb2pce_kb2040:
	$(call build_app,usb2pce_kb2040)

.PHONY: usb2gc_kb2040
usb2gc_kb2040:
	$(call build_app,usb2gc_kb2040)

.PHONY: usb2gc_rp2040zero
usb2gc_rp2040zero:
	$(call build_app,usb2gc_rp2040zero)

.PHONY: usb2nuon_kb2040
usb2nuon_kb2040:
	$(call build_app,usb2nuon_kb2040)

.PHONY: nuonserial_kb2040
nuonserial_kb2040:
	$(call build_app,nuonserial_kb2040)

.PHONY: usb2n64_kb2040
usb2n64_kb2040:
	$(call build_app,usb2n64_kb2040)

.PHONY: usb2loopy_kb2040
usb2loopy_kb2040:
	$(call build_app,usb2loopy_kb2040)

.PHONY: usb2dc_kb2040
usb2dc_kb2040:
	$(call build_app,usb2dc_kb2040)

.PHONY: usb2dc_rp2040zero
usb2dc_rp2040zero:
	$(call build_app,usb2dc_rp2040zero)

.PHONY: usb2neogeo_kb2040
usb2neogeo_kb2040:
	$(call build_app,usb2neogeo_kb2040)

.PHONY: usb2neogeo_pico
usb2neogeo_pico:
	$(call build_app,usb2neogeo_pico)

.PHONY: usb2neogeo_rp2040zero
usb2neogeo_rp2040zero:
	$(call build_app,usb2neogeo_rp2040zero)

.PHONY: n642dc_kb2040
n642dc_kb2040:
	$(call build_app,n642dc_kb2040)

.PHONY: n642dc_pico2_w
n642dc_pico2_w:
	$(call build_app,n642dc_pico2_w)

.PHONY: n642nuon_pico
n642nuon_pico:
	$(call build_app,n642nuon_pico)

.PHONY: n642nuon_aries64
n642nuon_aries64:
	$(call build_app,n642nuon_aries64)

.PHONY: usb23do_rp2040zero
usb23do_rp2040zero:
	$(call build_app,usb23do_rp2040zero)

.PHONY: snes23do_rp2040zero
snes23do_rp2040zero:
	$(call build_app,snes23do_rp2040zero)

.PHONY: usb2uart_kb2040
usb2uart_kb2040:
	$(call build_app,usb2uart_kb2040)

.PHONY: usb2usb_pico
usb2usb_pico:
	$(call build_app,usb2usb_pico)

.PHONY: usb2usb_pico_w
usb2usb_pico_w:
	$(call build_app,usb2usb_pico_w)

.PHONY: usb2usb_pico2_w
usb2usb_pico2_w:
	$(call build_app,usb2usb_pico2_w)

.PHONY: usb2usb_feather_rp2040
usb2usb_feather_rp2040:
	$(call build_app,usb2usb_feather_rp2040)

.PHONY: usb2usb_feather_rp2040_usb_host
usb2usb_feather_rp2040_usb_host:
	$(call build_app,usb2usb_feather_rp2040_usb_host)

.PHONY: usb2usb_feather_rp2040_max3421
usb2usb_feather_rp2040_max3421:
	$(call build_app,usb2usb_feather_rp2040_max3421)

.PHONY: usb2usb_feather_rp2040_usb_host_max3421
usb2usb_feather_rp2040_usb_host_max3421:
	$(call build_app,usb2usb_feather_rp2040_usb_host_max3421)

.PHONY: usb2usb_rp2040zero
usb2usb_rp2040zero:
	$(call build_app,usb2usb_rp2040zero)

.PHONY: usb2usb_rp2350usba
usb2usb_rp2350usba:
	$(call build_app,usb2usb_rp2350usba)

.PHONY: bt2usb_pico_w
bt2usb_pico_w:
	$(call build_app,bt2usb_pico_w)

.PHONY: bt2usb_pico2_w
bt2usb_pico2_w:
	$(call build_app,bt2usb_pico2_w)

.PHONY: bt2loopy_pico_w
bt2loopy_pico_w:
	$(call build_app,bt2loopy_pico_w)

.PHONY: bt2nuon_pico_w
bt2nuon_pico_w:
	$(call build_app,bt2nuon_pico_w)

.PHONY: bt2nuon_pico2_w
bt2nuon_pico2_w:
	$(call build_app,bt2nuon_pico2_w)

.PHONY: bt2n64_pico_w
bt2n64_pico_w:
	$(call build_app,bt2n64_pico_w)

.PHONY: bt2n64_pico2_w
bt2n64_pico2_w:
	$(call build_app,bt2n64_pico2_w)

.PHONY: bt2wiiext_pico_w
bt2wiiext_pico_w:
	$(call build_app,bt2wiiext_pico_w)

.PHONY: bt2gc_pico_w
bt2gc_pico_w:
	$(call build_app,bt2gc_pico_w)

.PHONY: bt2gc_pico2_w
bt2gc_pico2_w:
	$(call build_app,bt2gc_pico2_w)

.PHONY: btusb2usb_pico_w
btusb2usb_pico_w:
	$(call build_app,btusb2usb_pico_w)

.PHONY: btusb2usb_pico2_w
btusb2usb_pico2_w:
	$(call build_app,btusb2usb_pico2_w)

.PHONY: usb2ble_pico_w
usb2ble_pico_w:
	$(call build_app,usb2ble_pico_w)

.PHONY: usb2ble_pico2_w
usb2ble_pico2_w:
	$(call build_app,usb2ble_pico2_w)

# --- ESP32-S3 bt2usb (requires ESP-IDF) ---
.PHONY: bt2usb_xiao_esp32s3
bt2usb_xiao_esp32s3:
	@echo "$(YELLOW)Building bt2usb for XIAO ESP32-S3...$(NC)"
	@cd esp && $(MAKE) build
	@echo "$(GREEN)✓ bt2usb_xiao_esp32s3 built successfully$(NC)"
	@echo ""

.PHONY: flash-bt2usb_xiao_esp32s3
flash-bt2usb_xiao_esp32s3:
	@echo "$(YELLOW)Flashing bt2usb to XIAO ESP32-S3...$(NC)"
	@cd esp && $(MAKE) flash
	@echo "$(GREEN)✓ bt2usb_xiao_esp32s3 flashed successfully$(NC)"
	@echo ""

.PHONY: monitor-bt2usb_xiao_esp32s3
monitor-bt2usb_xiao_esp32s3:
	@cd esp && $(MAKE) monitor

# --- ESP32-S3 bt2usb on Feather ESP32-S3 (requires ESP-IDF) ---
.PHONY: bt2usb_feather_esp32s3
bt2usb_feather_esp32s3:
	@echo "$(YELLOW)Building bt2usb for Feather ESP32-S3...$(NC)"
	@cd esp && $(MAKE) build BOARD=feather_esp32s3
	@echo "$(GREEN)✓ bt2usb_feather_esp32s3 built successfully$(NC)"
	@echo ""

.PHONY: uf2-bt2usb_feather_esp32s3
uf2-bt2usb_feather_esp32s3:
	@echo "$(YELLOW)Building bt2usb UF2 for Feather ESP32-S3...$(NC)"
	@cd esp && $(MAKE) uf2 BOARD=feather_esp32s3
	@mkdir -p $(RELEASE_DIR)
	@cp esp/build/joypad_bt2usb.uf2 \
	    $(RELEASE_DIR)/joypad_$(VERSION_ID)_bt2usb_feather_esp32s3.uf2
	@echo "$(GREEN)✓ UF2 built: $(RELEASE_DIR)/joypad_$(VERSION_ID)_bt2usb_feather_esp32s3.uf2$(NC)"
	@echo ""

.PHONY: flash-bt2usb_feather_esp32s3
flash-bt2usb_feather_esp32s3:
	@echo "$(YELLOW)Flashing bt2usb to Feather ESP32-S3...$(NC)"
	@cd esp && $(MAKE) flash BOARD=feather_esp32s3
	@echo "$(GREEN)✓ bt2usb_feather_esp32s3 flashed successfully$(NC)"
	@echo ""

.PHONY: monitor-bt2usb_feather_esp32s3
monitor-bt2usb_feather_esp32s3:
	@cd esp && $(MAKE) monitor

# --- ESP32-S3 controller_btusb (requires ESP-IDF) ---
.PHONY: controller_btusb_feather_esp32s3
controller_btusb_feather_esp32s3:
	@echo "$(YELLOW)Building controller_btusb for Feather ESP32-S3...$(NC)"
	@cd esp && $(MAKE) build CONFIG_APP=controller_btusb BOARD=feather_esp32s3
	@echo "$(GREEN)✓ controller_btusb_feather_esp32s3 built successfully$(NC)"
	@echo ""

.PHONY: uf2-controller_btusb_feather_esp32s3
uf2-controller_btusb_feather_esp32s3:
	@echo "$(YELLOW)Building controller_btusb UF2 for Feather ESP32-S3...$(NC)"
	@cd esp && $(MAKE) uf2 CONFIG_APP=controller_btusb BOARD=feather_esp32s3
	@mkdir -p $(RELEASE_DIR)
	@cp esp/build/joypad_controller_btusb.uf2 \
	    $(RELEASE_DIR)/joypad_$(VERSION_ID)_controller_btusb_feather_esp32s3.uf2
	@echo "$(GREEN)✓ UF2 built: $(RELEASE_DIR)/joypad_$(VERSION_ID)_controller_btusb_feather_esp32s3.uf2$(NC)"
	@echo ""

.PHONY: flash-controller_btusb_feather_esp32s3
flash-controller_btusb_feather_esp32s3:
	@echo "$(YELLOW)Flashing controller_btusb to Feather ESP32-S3...$(NC)"
	@cd esp && $(MAKE) flash CONFIG_APP=controller_btusb BOARD=feather_esp32s3
	@echo "$(GREEN)✓ controller_btusb_feather_esp32s3 flashed successfully$(NC)"
	@echo ""

.PHONY: monitor-controller_btusb_feather_esp32s3
monitor-controller_btusb_feather_esp32s3:
	@cd esp && $(MAKE) monitor

# --- ESP32-S3 usb2usb on Feather ESP32-S3 (MAX3421E FeatherWing, requires ESP-IDF) ---
.PHONY: usb2usb_feather_esp32s3
usb2usb_feather_esp32s3:
	@echo "$(YELLOW)Building usb2usb for Feather ESP32-S3...$(NC)"
	@cd esp && $(MAKE) build CONFIG_APP=usb2usb BOARD=feather_esp32s3
	@echo "$(GREEN)✓ usb2usb_feather_esp32s3 built successfully$(NC)"
	@echo ""

.PHONY: uf2-usb2usb_feather_esp32s3
uf2-usb2usb_feather_esp32s3:
	@echo "$(YELLOW)Building usb2usb UF2 for Feather ESP32-S3...$(NC)"
	@cd esp && $(MAKE) uf2 CONFIG_APP=usb2usb BOARD=feather_esp32s3
	@mkdir -p $(RELEASE_DIR)
	@cp esp/build/joypad_usb2usb.uf2 \
	    $(RELEASE_DIR)/joypad_$(VERSION_ID)_usb2usb_feather_esp32s3.uf2
	@echo "$(GREEN)✓ UF2 built: $(RELEASE_DIR)/joypad_$(VERSION_ID)_usb2usb_feather_esp32s3.uf2$(NC)"
	@echo ""

.PHONY: flash-usb2usb_feather_esp32s3
flash-usb2usb_feather_esp32s3:
	@echo "$(YELLOW)Flashing usb2usb to Feather ESP32-S3...$(NC)"
	@cd esp && $(MAKE) flash CONFIG_APP=usb2usb BOARD=feather_esp32s3
	@echo "$(GREEN)✓ usb2usb_feather_esp32s3 flashed successfully$(NC)"
	@echo ""

.PHONY: monitor-usb2usb_feather_esp32s3
monitor-usb2usb_feather_esp32s3:
	@cd esp && $(MAKE) monitor

# --- ESP32-S3 btusb2usb on Feather ESP32-S3 (MAX3421E + BLE, requires ESP-IDF) ---
.PHONY: btusb2usb_feather_esp32s3
btusb2usb_feather_esp32s3:
	@echo "$(YELLOW)Building btusb2usb for Feather ESP32-S3...$(NC)"
	@cd esp && $(MAKE) build CONFIG_APP=btusb2usb BOARD=feather_esp32s3
	@echo "$(GREEN)✓ btusb2usb_feather_esp32s3 built successfully$(NC)"
	@echo ""

.PHONY: uf2-btusb2usb_feather_esp32s3
uf2-btusb2usb_feather_esp32s3:
	@echo "$(YELLOW)Building btusb2usb UF2 for Feather ESP32-S3...$(NC)"
	@cd esp && $(MAKE) uf2 CONFIG_APP=btusb2usb BOARD=feather_esp32s3
	@mkdir -p $(RELEASE_DIR)
	@cp esp/build/joypad_btusb2usb.uf2 \
	    $(RELEASE_DIR)/joypad_$(VERSION_ID)_btusb2usb_feather_esp32s3.uf2
	@echo "$(GREEN)✓ UF2 built: $(RELEASE_DIR)/joypad_$(VERSION_ID)_btusb2usb_feather_esp32s3.uf2$(NC)"
	@echo ""

.PHONY: flash-btusb2usb_feather_esp32s3
flash-btusb2usb_feather_esp32s3:
	@echo "$(YELLOW)Flashing btusb2usb to Feather ESP32-S3...$(NC)"
	@cd esp && $(MAKE) flash CONFIG_APP=btusb2usb BOARD=feather_esp32s3
	@echo "$(GREEN)✓ btusb2usb_feather_esp32s3 flashed successfully$(NC)"
	@echo ""

.PHONY: monitor-btusb2usb_feather_esp32s3
monitor-btusb2usb_feather_esp32s3:
	@cd esp && $(MAKE) monitor

# --- ESP32-S3 UF2 / Combined targets ---
.PHONY: uf2-bt2usb_xiao_esp32s3
uf2-bt2usb_xiao_esp32s3:
	@echo "$(YELLOW)Building bt2usb UF2 for ESP32-S3...$(NC)"
	@cd esp && $(MAKE) uf2
	@mkdir -p $(RELEASE_DIR)
	@cp esp/build/joypad_bt2usb.uf2 \
	    $(RELEASE_DIR)/joypad_$(VERSION_ID)_bt2usb_xiao_esp32s3.uf2
	@echo "$(GREEN)✓ UF2 built: $(RELEASE_DIR)/joypad_$(VERSION_ID)_bt2usb_xiao_esp32s3.uf2$(NC)"
	@echo ""

.PHONY: flash-uf2-bt2usb_xiao_esp32s3
flash-uf2-bt2usb_xiao_esp32s3: uf2-bt2usb_xiao_esp32s3
	@if [ ! -d "/Volumes/XIAOS3BOOT" ]; then \
		echo "$(YELLOW)⚠ /Volumes/XIAOS3BOOT not found$(NC)"; \
		echo "$(YELLOW)  Put device in TinyUF2 mode:$(NC)"; \
		echo "$(YELLOW)  - Double-tap reset button$(NC)"; \
		echo "$(YELLOW)  - Or send BOOTSEL via CDC$(NC)"; \
		exit 1; \
	fi
	@echo "$(YELLOW)Flashing UF2 to TinyUF2 drive...$(NC)"
	@cp $(RELEASE_DIR)/joypad_$(VERSION_ID)_bt2usb_xiao_esp32s3.uf2 /Volumes/XIAOS3BOOT/
	@echo "$(GREEN)✓ Firmware flashed, device will reboot$(NC)"
	@echo ""

# --- Seeed XIAO nRF52840 bt2usb (requires nRF Connect SDK) ---
.PHONY: bt2usb_seeed_xiao_nrf52840
bt2usb_seeed_xiao_nrf52840:
	@echo "$(YELLOW)Building bt2usb for Seeed XIAO nRF52840...$(NC)"
	@cd nrf && $(MAKE) build BOARD=xiao_ble
	@mkdir -p $(RELEASE_DIR)
	@cp nrf/build/nrf/zephyr/zephyr.uf2 \
	    $(RELEASE_DIR)/joypad_$(VERSION_ID)_bt2usb_seeed_xiao_nrf52840.uf2
	@echo "$(GREEN)✓ bt2usb_seeed_xiao_nrf52840 built successfully$(NC)"
	@echo "  File: $(RELEASE_DIR)/joypad_$(VERSION_ID)_bt2usb_seeed_xiao_nrf52840.uf2"
	@echo ""

.PHONY: flash-bt2usb_seeed_xiao_nrf52840
flash-bt2usb_seeed_xiao_nrf52840: bt2usb_seeed_xiao_nrf52840
	@cd nrf && $(MAKE) flash-uf2
	@echo ""

.PHONY: monitor-bt2usb_seeed_xiao_nrf52840
monitor-bt2usb_seeed_xiao_nrf52840:
	@cd nrf && $(MAKE) monitor

# --- Adafruit Feather nRF52840 Express bt2usb (requires nRF Connect SDK) ---
.PHONY: bt2usb_feather_nrf52840
bt2usb_feather_nrf52840:
	@echo "$(YELLOW)Building bt2usb for Adafruit Feather nRF52840...$(NC)"
	@cd nrf && $(MAKE) build BOARD=adafruit_feather_nrf52840
	@mkdir -p $(RELEASE_DIR)
	@cp nrf/build/nrf/zephyr/zephyr.uf2 \
	    $(RELEASE_DIR)/joypad_$(VERSION_ID)_bt2usb_feather_nrf52840.uf2
	@echo "$(GREEN)✓ bt2usb_feather_nrf52840 built successfully$(NC)"
	@echo "  File: $(RELEASE_DIR)/joypad_$(VERSION_ID)_bt2usb_feather_nrf52840.uf2"
	@echo ""

.PHONY: flash-bt2usb_feather_nrf52840
flash-bt2usb_feather_nrf52840: bt2usb_feather_nrf52840
	@cd nrf && $(MAKE) flash-uf2
	@echo ""

.PHONY: monitor-bt2usb_feather_nrf52840
monitor-bt2usb_feather_nrf52840:
	@cd nrf && $(MAKE) monitor

# --- Adafruit Feather nRF52840 usb2usb (MAX3421E FeatherWing, requires nRF Connect SDK) ---
.PHONY: usb2usb_feather_nrf52840
usb2usb_feather_nrf52840:
	@echo "$(YELLOW)Building usb2usb for Adafruit Feather nRF52840...$(NC)"
	@cd nrf && $(MAKE) build BOARD=adafruit_feather_nrf52840 APP_TYPE=usb2usb
	@mkdir -p $(RELEASE_DIR)
	@cp nrf/build/nrf/zephyr/zephyr.uf2 \
	    $(RELEASE_DIR)/joypad_$(VERSION_ID)_usb2usb_feather_nrf52840.uf2
	@echo "$(GREEN)✓ usb2usb_feather_nrf52840 built successfully$(NC)"
	@echo "  File: $(RELEASE_DIR)/joypad_$(VERSION_ID)_usb2usb_feather_nrf52840.uf2"
	@echo ""

.PHONY: flash-usb2usb_feather_nrf52840
flash-usb2usb_feather_nrf52840: usb2usb_feather_nrf52840
	@cd nrf && $(MAKE) flash-uf2
	@echo ""

.PHONY: monitor-usb2usb_feather_nrf52840
monitor-usb2usb_feather_nrf52840:
	@cd nrf && $(MAKE) monitor

# --- Adafruit Feather nRF52840 btusb2usb (MAX3421E + BLE, requires nRF Connect SDK) ---
.PHONY: btusb2usb_feather_nrf52840
btusb2usb_feather_nrf52840:
	@echo "$(YELLOW)Building btusb2usb for Adafruit Feather nRF52840...$(NC)"
	@cd nrf && $(MAKE) build BOARD=adafruit_feather_nrf52840 APP_TYPE=btusb2usb
	@mkdir -p $(RELEASE_DIR)
	@cp nrf/build/nrf/zephyr/zephyr.uf2 \
	    $(RELEASE_DIR)/joypad_$(VERSION_ID)_btusb2usb_feather_nrf52840.uf2
	@echo "$(GREEN)✓ btusb2usb_feather_nrf52840 built successfully$(NC)"
	@echo "  File: $(RELEASE_DIR)/joypad_$(VERSION_ID)_btusb2usb_feather_nrf52840.uf2"
	@echo ""

.PHONY: flash-btusb2usb_feather_nrf52840
flash-btusb2usb_feather_nrf52840: btusb2usb_feather_nrf52840
	@cd nrf && $(MAKE) flash-uf2
	@echo ""

.PHONY: monitor-btusb2usb_feather_nrf52840
monitor-btusb2usb_feather_nrf52840:
	@cd nrf && $(MAKE) monitor

# --- nRF52840 controller_btusb on Feather nRF52840 (sensor + BLE peripheral + USB) ---
.PHONY: controller_btusb_feather_nrf52840
controller_btusb_feather_nrf52840:
	@echo "$(YELLOW)Building controller_btusb for Feather nRF52840...$(NC)"
	@cd nrf && $(MAKE) build BOARD=adafruit_feather_nrf52840 APP_TYPE=controller_btusb
	@mkdir -p $(RELEASE_DIR)
	@cp nrf/build/nrf/zephyr/zephyr.uf2 \
	    $(RELEASE_DIR)/joypad_$(VERSION_ID)_controller_btusb_feather_nrf52840.uf2
	@echo "$(GREEN)✓ controller_btusb_feather_nrf52840 built successfully$(NC)"
	@echo "  File: $(RELEASE_DIR)/joypad_$(VERSION_ID)_controller_btusb_feather_nrf52840.uf2"
	@echo ""

.PHONY: flash-controller_btusb_feather_nrf52840
flash-controller_btusb_feather_nrf52840: controller_btusb_feather_nrf52840
	@cd nrf && $(MAKE) flash-uf2

.PHONY: monitor-controller_btusb_feather_nrf52840
monitor-controller_btusb_feather_nrf52840:
	@cd nrf && $(MAKE) monitor

.PHONY: wifi2usb_pico_w
wifi2usb_pico_w:
	$(call build_app,wifi2usb_pico_w)

.PHONY: wifi2usb_pico2_w
wifi2usb_pico2_w:
	$(call build_app,wifi2usb_pico2_w)

.PHONY: snes2usb_kb2040
snes2usb_kb2040:
	$(call build_app,snes2usb_kb2040)

.PHONY: n642usb_kb2040
n642usb_kb2040:
	$(call build_app,n642usb_kb2040)

.PHONY: nuon2usb_kb2040
nuon2usb_kb2040:
	$(call build_app,nuon2usb_kb2040)

.PHONY: nuon2usb_pico_w
nuon2usb_pico_w:
	$(call build_app,nuon2usb_pico_w)

.PHONY: gc2usb_kb2040
gc2usb_kb2040:
	$(call build_app,gc2usb_kb2040)

.PHONY: gc2usb_rp2040zero
gc2usb_rp2040zero:
	$(call build_app,gc2usb_rp2040zero)

.PHONY: gc2usb_pico
gc2usb_pico:
	$(call build_app,gc2usb_pico)

.PHONY: wii2usb_kb2040
wii2usb_kb2040:
	$(call build_app,wii2usb_kb2040)

.PHONY: wii2gc_kb2040
wii2gc_kb2040:
	$(call build_app,wii2gc_kb2040)

.PHONY: wii2n64_pico
wii2n64_pico:
	$(call build_app,wii2n64_pico)

.PHONY: neogeo2usb_kb2040
neogeo2usb_kb2040:
	$(call build_app,neogeo2usb_kb2040)

.PHONY: neogeo2usb_rp2040zero
neogeo2usb_rp2040zero:
	$(call build_app,neogeo2usb_rp2040zero)

.PHONY: jvs2usb_rp2040zero
jvs2usb_rp2040zero:
	$(call build_app,jvs2usb_rp2040zero)

.PHONY: controller_fisherprice_v1_kb2040
controller_fisherprice_v1_kb2040:
	$(call build_app,controller_fisherprice_v1_kb2040)

.PHONY: controller_fisherprice_v2_kb2040
controller_fisherprice_v2_kb2040:
	$(call build_app,controller_fisherprice_v2_kb2040)

.PHONY: controller_alpakka_pico
controller_alpakka_pico:
	$(call build_app,controller_alpakka_pico)

.PHONY: controller_macropad
controller_macropad:
	$(call build_app,controller_macropad)

.PHONY: controller_btusb_fisherprice_v1_kb2040
controller_btusb_fisherprice_v1_kb2040:
	$(call build_app,controller_btusb_fisherprice_v1_kb2040)

.PHONY: controller_btusb_fisherprice_v2_kb2040
controller_btusb_fisherprice_v2_kb2040:
	$(call build_app,controller_btusb_fisherprice_v2_kb2040)

.PHONY: controller_btusb_alpakka_pico
controller_btusb_alpakka_pico:
	$(call build_app,controller_btusb_alpakka_pico)

.PHONY: controller_btusb_pico_w
controller_btusb_pico_w:
	$(call build_app,controller_btusb_pico_w)

.PHONY: controller_btusb_pico2_w
controller_btusb_pico2_w:
	$(call build_app,controller_btusb_pico2_w)

.PHONY: controller_btusb_rp2040_abb
controller_btusb_rp2040_abb:
	$(call build_app,controller_btusb_rp2040_abb)

.PHONY: controller_btusb_feather_rp2040
controller_btusb_feather_rp2040:
	$(call build_app,controller_btusb_feather_rp2040)

.PHONY: controller_btusb_feather_rp2040_usb_host
controller_btusb_feather_rp2040_usb_host:
	$(call build_app,controller_btusb_feather_rp2040_usb_host)


.PHONY: nes2usb_kb2040
nes2usb_kb2040:
	$(call build_app,nes2usb_kb2040)

.PHONY: nes2usb_pico_w
nes2usb_pico_w:
	$(call build_app,nes2usb_pico_w)

.PHONY: lodgenet2usb_pico
lodgenet2usb_pico:
	$(call build_app,lodgenet2usb_pico)

.PHONY: lodgenet2usb_pico2
lodgenet2usb_pico2:
	$(call build_app,lodgenet2usb_pico2)

.PHONY: lodgenet2n64_pico
lodgenet2n64_pico:
	$(call build_app,lodgenet2n64_pico)

.PHONY: lodgenet2gc_pico
lodgenet2gc_pico:
	$(call build_app,lodgenet2gc_pico)

# Console-only targets (defaults to KB2040)
.PHONY: 3do
3do:
	@echo "$(YELLOW)Building 3DO (KB2040)...$(NC)"
	@cd src && rm -rf build && sh $(BOARD_SCRIPT_kb2040)
	@cd src/build && $(MAKE) --no-print-directory $(CONSOLE_3do) -j$(JOBS)
	@echo "$(GREEN)✓ 3DO built successfully$(NC)"
	@echo "  Output: src/build/$(CONSOLE_3do).uf2"
	@echo ""

.PHONY: pce
pce:
	@echo "$(YELLOW)Building PCEngine (KB2040)...$(NC)"
	@cd src && rm -rf build && sh $(BOARD_SCRIPT_kb2040)
	@cd src/build && $(MAKE) --no-print-directory $(CONSOLE_pce) -j$(JOBS)
	@echo "$(GREEN)✓ PCEngine built successfully$(NC)"
	@echo "  Output: src/build/$(CONSOLE_pce).uf2"
	@echo ""

.PHONY: ngc
ngc:
	@echo "$(YELLOW)Building GameCube (KB2040)...$(NC)"
	@cd src && rm -rf build && sh $(BOARD_SCRIPT_kb2040)
	@cd src/build && $(MAKE) --no-print-directory $(CONSOLE_ngc) -j$(JOBS)
	@echo "$(GREEN)✓ GameCube built successfully$(NC)"
	@echo "  Output: src/build/$(CONSOLE_ngc).uf2"
	@echo ""

.PHONY: ngc_rp2040zero
ngc_rp2040zero:
	@echo "$(YELLOW)Building GameCube (RP2040-Zero)...$(NC)"
	@cd src && rm -rf build && sh $(BOARD_SCRIPT_rp2040zero)
	@cd src/build && $(MAKE) --no-print-directory $(CONSOLE_ngc) -j$(JOBS)
	@echo "$(GREEN)✓ GameCube built successfully$(NC)"
	@echo "  Output: src/build/$(CONSOLE_ngc).uf2"
	@echo ""

.PHONY: nuon
nuon:
	@echo "$(YELLOW)Building Nuon (KB2040)...$(NC)"
	@cd src && rm -rf build && sh $(BOARD_SCRIPT_kb2040)
	@cd src/build && $(MAKE) --no-print-directory $(CONSOLE_nuon) -j$(JOBS)
	@echo "$(GREEN)✓ Nuon built successfully$(NC)"
	@echo "  Output: src/build/$(CONSOLE_nuon).uf2"
	@echo ""

.PHONY: loopy
loopy:
	@echo "$(YELLOW)Building Loopy (KB2040)...$(NC)"
	@cd src && rm -rf build && sh $(BOARD_SCRIPT_kb2040)
	@cd src/build && $(MAKE) --no-print-directory $(CONSOLE_loopy) -j$(JOBS)
	@echo "$(GREEN)✓ Loopy built successfully$(NC)"
	@echo "  Output: src/build/$(CONSOLE_loopy).uf2"
	@echo ""

.PHONY: neogeo
neogeo:
	@echo "$(YELLOW)Building NEOGEO (KB2040)...$(NC)"
	@cd src && rm -rf build && sh $(BOARD_SCRIPT_kb2040)
	@cd src/build && $(MAKE) --no-print-directory $(CONSOLE_neogeo) -j$(JOBS)
	@echo "$(GREEN)✓ NEOGEO built successfully$(NC)"
	@echo "  Output: src/build/$(CONSOLE_neogeo).uf2"
	@echo ""

# Build all apps
.PHONY: all
all: $(APPS)
	@echo "$(BLUE)==============================$(NC)"
	@echo "$(GREEN)All apps built!$(NC)"
	@echo "$(BLUE)==============================$(NC)"
	@ls -lh $(RELEASE_DIR)/*.uf2
	@echo ""

# Build only stable apps for release
.PHONY: releases
releases: $(RELEASE_APPS)
	@echo "$(BLUE)==============================$(NC)"
	@echo "$(GREEN)Release apps built!$(NC)"
	@echo "$(BLUE)==============================$(NC)"
	@ls -lh $(RELEASE_DIR)/*.uf2
	@echo ""

# Flash target - flashes most recently built firmware
.PHONY: flash
flash:
	@# Check for either RPI-RP2 (RP2040) or RP2350 volume
	@if [ -d "/Volumes/RP2350" ]; then \
		VOLUME="/Volumes/RP2350"; \
	elif [ -d "/Volumes/RPI-RP2" ]; then \
		VOLUME="/Volumes/RPI-RP2"; \
	else \
		echo "$(YELLOW)⚠ No RP2040/RP2350 drive found$(NC)"; \
		echo "$(YELLOW)  Please put device in bootloader mode:$(NC)"; \
		echo "$(YELLOW)  - Hold BOOT button while plugging in USB-C$(NC)"; \
		echo "$(YELLOW)  - Or unplug all USB devices and plug in USB-C$(NC)"; \
		exit 1; \
	fi; \
	LATEST_UF2=$$(ls -t $(RELEASE_DIR)/*.uf2 2>/dev/null | head -1); \
	if [ -z "$$LATEST_UF2" ]; then \
		echo "$(YELLOW)⚠ No UF2 files found in $(RELEASE_DIR)$(NC)"; \
		echo "$(YELLOW)  Run 'make usb2pce' or another build target first$(NC)"; \
		exit 1; \
	fi; \
	echo "$(YELLOW)Flashing $$(basename $$LATEST_UF2)...$(NC)"; \
	cp "$$LATEST_UF2" "$$VOLUME/" && \
	echo "$(GREEN)✓ Firmware flashed successfully!$(NC)" && \
	echo "$(GREEN)  Device will reboot automatically$(NC)"

# Flash specific apps (finds most recent matching file)
.PHONY: flash-usb2pce_kb2040
flash-usb2pce_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2pce_kb2040

.PHONY: flash-usb2gc_kb2040
flash-usb2gc_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2gc_kb2040

.PHONY: flash-usb2gc_rp2040zero
flash-usb2gc_rp2040zero:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2gc_rp2040zero

.PHONY: flash-usb2neogeo_kb2040
flash-usb2neogeo_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2neogeo_kb2040

.PHONY: flash-usb2neogeo_pico
flash-usb2neogeo_pico:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2neogeo_pico

.PHONY: flash-usb2neogeo_rp2040zero
flash-usb2neogeo_rp2040zero:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2neogeo_rp2040zero

.PHONY: flash-usb2nuon_kb2040
flash-usb2nuon_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2nuon_kb2040

.PHONY: flash-nuonserial_kb2040
flash-nuonserial_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=nuonserial_kb2040

.PHONY: flash-usb2loopy_kb2040
flash-usb2loopy_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2loopy_kb2040

.PHONY: flash-usb2dc_kb2040
flash-usb2dc_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2dc_kb2040

.PHONY: flash-usb2dc_rp2040zero
flash-usb2dc_rp2040zero:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2dc_rp2040zero

.PHONY: flash-n642dc_kb2040
flash-n642dc_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=n642dc_kb2040

.PHONY: flash-n642dc_pico2_w
flash-n642dc_pico2_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=n642dc_pico2_w

.PHONY: flash-n642nuon_pico
flash-n642nuon_pico:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=n642nuon_pico

.PHONY: flash-n642nuon_aries64
flash-n642nuon_aries64:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=n642nuon_aries64

.PHONY: flash-usb23do_rp2040zero
flash-usb23do_rp2040zero:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb23do_rp2040zero

.PHONY: flash-snes23do_rp2040zero
flash-snes23do_rp2040zero:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=snes23do_rp2040zero

.PHONY: flash-usb2uart_kb2040
flash-usb2uart_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2uart_kb2040

.PHONY: flash-usb2usb_pico
flash-usb2usb_pico:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2usb_pico

.PHONY: flash-usb2usb_pico_w
flash-usb2usb_pico_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2usb_pico_w

.PHONY: flash-usb2usb_pico2_w
flash-usb2usb_pico2_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2usb_pico2_w

.PHONY: flash-usb2usb_feather_rp2040
flash-usb2usb_feather_rp2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2usb_feather_rp2040

.PHONY: flash-usb2usb_feather_rp2040_usb_host
flash-usb2usb_feather_rp2040_usb_host:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2usb_feather_rp2040_usb_host

.PHONY: flash-usb2usb_feather_rp2040_max3421
flash-usb2usb_feather_rp2040_max3421:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2usb_feather_rp2040_max3421

.PHONY: flash-usb2usb_feather_rp2040_usb_host_max3421
flash-usb2usb_feather_rp2040_usb_host_max3421:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2usb_feather_rp2040_usb_host_max3421

.PHONY: flash-usb2usb_rp2040zero
flash-usb2usb_rp2040zero:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2usb_rp2040zero

.PHONY: flash-usb2usb_rp2350usba
flash-usb2usb_rp2350usba:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2usb_rp2350usba

.PHONY: flash-bt2usb_pico_w
flash-bt2usb_pico_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=bt2usb_pico_w

.PHONY: flash-bt2usb_pico2_w
flash-bt2usb_pico2_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=bt2usb_pico2_w

.PHONY: flash-bt2loopy_pico_w
flash-bt2loopy_pico_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=bt2loopy_pico_w

.PHONY: flash-bt2nuon_pico_w
flash-bt2nuon_pico_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=bt2nuon_pico_w

.PHONY: flash-bt2nuon_pico2_w
flash-bt2nuon_pico2_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=bt2nuon_pico2_w

.PHONY: flash-bt2n64_pico_w
flash-bt2n64_pico_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=bt2n64_pico_w

.PHONY: flash-bt2n64_pico2_w
flash-bt2n64_pico2_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=bt2n64_pico2_w

.PHONY: flash-bt2wiiext_pico_w
flash-bt2wiiext_pico_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=bt2wiiext_pico_w

.PHONY: flash-bt2gc_pico_w
flash-bt2gc_pico_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=bt2gc_pico_w

.PHONY: flash-bt2gc_pico2_w
flash-bt2gc_pico2_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=bt2gc_pico2_w

.PHONY: flash-btusb2usb_pico_w
flash-btusb2usb_pico_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=btusb2usb_pico_w

.PHONY: flash-btusb2usb_pico2_w
flash-btusb2usb_pico2_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=btusb2usb_pico2_w

.PHONY: flash-usb2ble_pico_w
flash-usb2ble_pico_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2ble_pico_w

.PHONY: flash-usb2ble_pico2_w
flash-usb2ble_pico2_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2ble_pico2_w

.PHONY: flash-usb2n64_kb2040
flash-usb2n64_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2n64_kb2040

.PHONY: flash-wifi2usb_pico_w
flash-wifi2usb_pico_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=wifi2usb_pico_w

.PHONY: flash-wifi2usb_pico2_w
flash-wifi2usb_pico2_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=wifi2usb_pico2_w

.PHONY: flash-snes2usb_kb2040
flash-snes2usb_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=snes2usb_kb2040

.PHONY: flash-n642usb_kb2040
flash-n642usb_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=n642usb_kb2040

.PHONY: flash-nuon2usb_kb2040
flash-nuon2usb_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=nuon2usb_kb2040

.PHONY: flash-nuon2usb_pico_w
flash-nuon2usb_pico_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=nuon2usb_pico_w

.PHONY: flash-gc2usb_kb2040
flash-gc2usb_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=gc2usb_kb2040

.PHONY: flash-gc2usb_pico
flash-gc2usb_pico:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=gc2usb_pico

.PHONY: flash-wii2usb_kb2040
flash-wii2usb_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=wii2usb_kb2040

.PHONY: flash-wii2gc_kb2040
flash-wii2gc_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=wii2gc_kb2040

.PHONY: flash-wii2n64_pico
flash-wii2n64_pico:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=wii2n64_pico

.PHONY: flash-neogeo2usb_kb2040
flash-neogeo2usb_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=neogeo2usb_kb2040

.PHONY: flash-neogeo2usb_rp2040zero
flash-neogeo2usb_rp2040zero:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=neogeo2usb_rp2040zero

.PHONY: flash-jvs2usb_rp2040zero
flash-jvs2usb_rp2040zero:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=jvs2usb_rp2040zero

.PHONY: flash-controller_fisherprice_v1_kb2040
flash-controller_fisherprice_v1_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=controller_fisherprice_v1_kb2040

.PHONY: flash-controller_fisherprice_v2_kb2040
flash-controller_fisherprice_v2_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=controller_fisherprice_v2_kb2040

.PHONY: flash-controller_alpakka_pico
flash-controller_alpakka_pico:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=controller_alpakka_pico

.PHONY: flash-controller_macropad
flash-controller_macropad:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=controller_macropad

.PHONY: flash-controller_btusb_pico_w
flash-controller_btusb_pico_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=controller_btusb_pico_w

.PHONY: flash-controller_btusb_pico2_w
flash-controller_btusb_pico2_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=controller_btusb_pico2_w

.PHONY: flash-controller_btusb_rp2040_abb
flash-controller_btusb_rp2040_abb:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=controller_btusb_rp2040_abb

.PHONY: flash-controller_btusb_feather_rp2040
flash-controller_btusb_feather_rp2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=controller_btusb_feather_rp2040

.PHONY: flash-controller_btusb_feather_rp2040_usb_host
flash-controller_btusb_feather_rp2040_usb_host:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=controller_btusb_feather_rp2040_usb_host


.PHONY: flash-nes2usb_kb2040
flash-nes2usb_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=nes2usb_kb2040

.PHONY: flash-nes2usb_pico_w
flash-nes2usb_pico_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=nes2usb_pico_w

.PHONY: flash-lodgenet2usb_pico
flash-lodgenet2usb_pico:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=lodgenet2usb_pico

.PHONY: flash-lodgenet2usb_pico2
flash-lodgenet2usb_pico2:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=lodgenet2usb_pico2

.PHONY: flash-lodgenet2n64_pico
flash-lodgenet2n64_pico:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=lodgenet2n64_pico

.PHONY: flash-lodgenet2gc_pico
flash-lodgenet2gc_pico:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=lodgenet2gc_pico

# Internal flash helper for specific app (finds most recent matching file)
.PHONY: _flash_app
_flash_app:
	@# Determine correct volume based on app name (RP2350/Pico2 boards use different volume)
	@if echo "$(APP_NAME)" | grep -qE "rp2350|pico2"; then \
		VOLUME="/Volumes/RP2350"; \
	else \
		VOLUME="/Volumes/RPI-RP2"; \
	fi; \
	if [ ! -d "$$VOLUME" ]; then \
		echo "$(YELLOW)⚠ $$VOLUME drive not found$(NC)"; \
		echo "$(YELLOW)  Please put device in bootloader mode$(NC)"; \
		exit 1; \
	fi; \
	FLASH_FILE=$$(ls -t $(RELEASE_DIR)/joypad_*$(APP_NAME).uf2 2>/dev/null | head -1); \
	if [ -z "$$FLASH_FILE" ]; then \
		echo "$(YELLOW)⚠ No $(APP_NAME) firmware found in $(RELEASE_DIR)$(NC)"; \
		echo "$(YELLOW)  Build it first with 'make $(APP_NAME)'$(NC)"; \
		exit 1; \
	fi; \
	echo "$(YELLOW)Flashing $$(basename $$FLASH_FILE)...$(NC)"; \
	cp "$$FLASH_FILE" "$$VOLUME/" && \
	echo "$(GREEN)✓ Firmware flashed successfully!$(NC)" && \
	echo "$(GREEN)  Device will reboot automatically$(NC)"

# Clean target
.PHONY: clean
clean:
	@echo "$(YELLOW)Cleaning build artifacts...$(NC)"
	@rm -rf src/build
	@rm -rf $(RELEASE_DIR)
	@echo "$(GREEN)✓ Clean complete$(NC)"
	@echo ""

clean-esp:
	@echo "$(YELLOW)Cleaning ESP32 build artifacts...$(NC)"
	@rm -rf esp/build esp/sdkconfig
	@echo "$(GREEN)✓ ESP32 clean complete$(NC)"
	@echo ""

# Full clean - reset to fresh clone state
.PHONY: fullclean
fullclean:
	@echo "$(YELLOW)⚠️  Full clean - resetting to fresh clone state...$(NC)"
	@echo "$(YELLOW)  This will remove all untracked files and deinitialize submodules!$(NC)"
	@rm -rf src/build
	@rm -rf $(RELEASE_DIR)
	@git clean -fdx
	@git submodule deinit -f --all
	@echo "$(GREEN)✓ Full clean complete - repository reset to fresh clone state$(NC)"
	@echo "$(GREEN)  Run 'make init' to initialize submodules and start building$(NC)"
	@echo ""

# CDC protocol test tool (interactive serial console)
# Auto-detects USB CDC serial port, or pass PORT= to override
.PHONY: cdc-test
cdc-test:
	@CDC_PORT=$${PORT:-$$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)}; \
	if [ -z "$$CDC_PORT" ]; then \
		echo "$(RED)✗ No USB CDC device found$(NC)"; \
		echo "  Connect a device in CDC mode, or specify: make cdc-test PORT=/dev/cu.usbmodemXXX"; \
		exit 1; \
	fi; \
	echo "$(BLUE)Connecting to $$CDC_PORT...$(NC)"; \
	python3 tools/cdc_test.py "$$CDC_PORT"

# Show current configuration
.PHONY: config
config:
	@echo "$(BLUE)Current Configuration:$(NC)"
	@echo "  PICO_SDK_PATH:       $(PICO_SDK_PATH)"
	@echo "  PICO_TOOLCHAIN_PATH: $(PICO_TOOLCHAIN_PATH)"
	@echo ""

# Serve docs locally (MkDocs)
DOCS_VENV := .venv-docs
.PHONY: serve-docs
serve-docs:
	@if [ ! -d "$(DOCS_VENV)" ]; then \
		echo "Setting up docs environment..."; \
		python3 -m venv $(DOCS_VENV) && \
		$(DOCS_VENV)/bin/pip install mkdocs mkdocs-material; \
	fi
	@echo "Serving docs at http://127.0.0.1:8000"
	@$(DOCS_VENV)/bin/mkdocs serve

# Serve web config locally
.PHONY: serve-web
serve-web:
	@echo "Serving web config at http://127.0.0.1:8080"
	@cd tools/web-config/src && python3 -c "\
	import http.server; \
	http.server.SimpleHTTPRequestHandler.extensions_map.update({'.js': 'application/javascript', '.mjs': 'application/javascript'}); \
	http.server.HTTPServer(('', 8080), http.server.SimpleHTTPRequestHandler).serve_forever()"
