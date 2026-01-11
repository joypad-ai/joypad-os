# Joypad Top-Level Makefile
# Builds firmware for all product variants

# Default target
.DEFAULT_GOAL := help

# Ensure PICO_TOOLCHAIN_PATH is set
ifndef PICO_TOOLCHAIN_PATH
    # Try macOS default location
    TOOLCHAIN_PATH_MACOS := /Applications/ArmGNUToolchain/14.2.rel1/arm-none-eabi
    # Try Linux/CI location (toolchain in PATH)
    TOOLCHAIN_IN_PATH := $(shell which arm-none-eabi-gcc 2>/dev/null)

    ifneq ($(wildcard $(TOOLCHAIN_PATH_MACOS)),)
        export PICO_TOOLCHAIN_PATH := $(TOOLCHAIN_PATH_MACOS)
    else ifneq ($(TOOLCHAIN_IN_PATH),)
        # Toolchain is in PATH (Linux/Docker/CI) - pico-sdk will find it automatically
        export PICO_TOOLCHAIN_PATH :=
    else
        $(error PICO_TOOLCHAIN_PATH not set and toolchain not found in PATH or at $(TOOLCHAIN_PATH_MACOS))
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
BOARD_SCRIPT_pico2_w := boards/build_pico2_w.sh
BOARD_SCRIPT_kb2040 := boards/build_ada_kb2040.sh
BOARD_SCRIPT_qtpy := boards/build_ada_qtpy.sh
BOARD_SCRIPT_rp2040zero := boards/build_waveshare_rp2040_zero.sh
BOARD_SCRIPT_feather_usbhost := boards/build_ada_feather_usbhost.sh
BOARD_SCRIPT_macropad := boards/build_ada_macropad.sh
BOARD_SCRIPT_rp2350usba := boards/build_waveshare_rp2350_usb_a.sh

# Console targets (cmake target names)
CONSOLE_3do := joypad_3do
CONSOLE_pce := joypad_pce
CONSOLE_ngc := joypad_ngc
CONSOLE_ngc_rp2040zero := joypad_ngc_rp2040zero
CONSOLE_nuon := joypad_nuon
CONSOLE_loopy := joypad_loopy
CONSOLE_dc := joypad_dc
CONSOLE_n642dc := joypad_n642dc
CONSOLE_snes3do := joypad_snes3do
CONSOLE_uart := joypad_uart
CONSOLE_usb := joypad_usb
CONSOLE_usb_rp2040zero := joypad_usb_rp2040zero
CONSOLE_usb_rp2350usba := joypad_usb_rp2350usba
CONSOLE_bt2usb := joypad_bt2usb
CONSOLE_snes2usb := joypad_snes2usb
CONSOLE_controller_fisherprice := joypad_controller_fisherprice
CONSOLE_controller_fisherprice_analog := joypad_controller_fisherprice_analog
CONSOLE_controller_alpakka := joypad_controller_alpakka
CONSOLE_controller_macropad := joypad_controller_macropad

# App definitions: APP_name = board target output_name input output
# Naming convention: <app>_<board> for all apps
APP_usb2pce_kb2040 := kb2040 pce usb2pce_kb2040 USB/BT PCEngine
APP_usb2gc_kb2040 := kb2040 ngc usb2gc_kb2040 USB/BT GameCube
APP_usb2gc_rp2040zero := rp2040zero ngc_rp2040zero usb2gc_rp2040zero USB/BT GameCube
APP_usb2nuon_kb2040 := kb2040 nuon usb2nuon_kb2040 USB/BT Nuon
APP_usb2loopy_kb2040 := kb2040 loopy usb2loopy_kb2040 USB/BT Loopy
APP_usb2dc_kb2040 := kb2040 dc usb2dc_kb2040 USB/BT Dreamcast
APP_n642dc_kb2040 := kb2040 n642dc n642dc_kb2040 N64 Dreamcast
APP_usb23do_rp2040zero := rp2040zero 3do usb23do_rp2040zero USB/BT 3DO
APP_snes23do_rp2040zero := rp2040zero snes3do snes23do_rp2040zero SNES 3DO
APP_usb2uart_kb2040 := kb2040 uart usb2uart_kb2040 USB/BT UART
APP_usb2usb_feather := feather_usbhost usb usb2usb_feather USB/BT USB
APP_usb2usb_rp2040zero := rp2040zero usb_rp2040zero usb2usb_rp2040zero USB/BT USB
APP_usb2usb_rp2350usba := rp2350usba usb_rp2350usba usb2usb_rp2350usba USB/BT USB
APP_bt2usb_pico_w := pico_w bt2usb bt2usb_pico_w Bluetooth USB
APP_bt2usb_pico2_w := pico2_w bt2usb bt2usb_pico2_w Bluetooth USB
APP_snes2usb_kb2040 := kb2040 snes2usb snes2usb_kb2040 SNES USB
APP_controller_fisherprice_kb2040 := kb2040 controller_fisherprice controller_fisherprice_kb2040 GPIO USB
APP_controller_fisherprice_analog_kb2040 := kb2040 controller_fisherprice_analog controller_fisherprice_analog_kb2040 GPIO/ADC USB
APP_controller_alpakka_pico := pico controller_alpakka controller_alpakka_pico GPIO/I2C USB
APP_controller_macropad := macropad controller_macropad controller_macropad GPIO USB

# All apps (note: controller_macropad not included - build explicitly with 'make controller_macropad')
APPS := usb2pce_kb2040 usb2gc_kb2040 usb2gc_rp2040zero usb2nuon_kb2040 usb2loopy_kb2040 usb2dc_kb2040 usb23do_rp2040zero snes23do_rp2040zero usb2uart_kb2040 usb2usb_feather usb2usb_rp2040zero usb2usb_rp2350usba bt2usb_pico_w bt2usb_pico2_w snes2usb_kb2040 controller_fisherprice_kb2040 controller_alpakka_pico

# Stable apps for release
RELEASE_APPS := usb2pce_kb2040 usb2gc_kb2040 usb2gc_rp2040zero usb2nuon_kb2040 usb2loopy_kb2040 usb23do_rp2040zero usb2usb_feather usb2usb_rp2040zero bt2usb_pico_w snes2usb_kb2040 snes23do_rp2040zero

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
	@echo "  make build         - Build all apps (alias for 'make all')"
	@echo ""
	@echo "$(GREEN)App Targets:$(NC)"
	@echo "  make usb2pce_kb2040     - USB/BT -> PCEngine (KB2040)"
	@echo "  make usb2gc_kb2040      - USB/BT -> GameCube (KB2040)"
	@echo "  make usb2gc_rp2040zero  - USB/BT -> GameCube (RP2040-Zero)"
	@echo "  make usb2nuon_kb2040    - USB/BT -> Nuon (KB2040)"
	@echo "  make usb2loopy_kb2040   - USB/BT -> Loopy (KB2040)"
	@echo "  make usb2dc_kb2040      - USB/BT -> Dreamcast (KB2040)"
	@echo "  make n642dc_kb2040      - N64 -> Dreamcast (KB2040)"
	@echo "  make usb23do_rp2040zero - USB/BT -> 3DO (RP2040-Zero)"
	@echo "  make snes23do_rp2040zero - SNES -> 3DO (RP2040-Zero)"
	@echo "  make usb2uart_kb2040    - USB -> UART/ESP32 (KB2040)"
	@echo "  make usb2usb_feather    - USB/BT -> USB HID (Feather USB Host)"
	@echo "  make usb2usb_rp2040zero - USB/BT -> USB HID (RP2040-Zero)"
	@echo "  make usb2usb_rp2350usba - USB/BT -> USB HID (Waveshare RP2350A)"
	@echo "  make bt2usb_pico_w      - Bluetooth -> USB HID (Pico W)"
	@echo "  make snes2usb_kb2040    - SNES -> USB HID (KB2040)"
	@echo "  make controller_fisherprice_kb2040 - GPIO -> USB HID (KB2040)"
	@echo "  make controller_alpakka_pico - GPIO/I2C -> USB HID (Pico)"
	@echo "  make controller_macropad - 12 keys -> USB HID (MacroPad RP2040)"
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
	@cd src/build && $(MAKE) --no-print-directory $(CONSOLE_$(word 2,$(APP_$1))) -j4
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

.PHONY: usb2loopy_kb2040
usb2loopy_kb2040:
	$(call build_app,usb2loopy_kb2040)

.PHONY: usb2dc_kb2040
usb2dc_kb2040:
	$(call build_app,usb2dc_kb2040)

.PHONY: n642dc_kb2040
n642dc_kb2040:
	$(call build_app,n642dc_kb2040)

.PHONY: usb23do_rp2040zero
usb23do_rp2040zero:
	$(call build_app,usb23do_rp2040zero)

.PHONY: snes23do_rp2040zero
snes23do_rp2040zero:
	$(call build_app,snes23do_rp2040zero)

.PHONY: usb2uart_kb2040
usb2uart_kb2040:
	$(call build_app,usb2uart_kb2040)

.PHONY: usb2usb_feather
usb2usb_feather:
	$(call build_app,usb2usb_feather)

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

.PHONY: snes2usb_kb2040
snes2usb_kb2040:
	$(call build_app,snes2usb_kb2040)

.PHONY: controller_fisherprice_kb2040
controller_fisherprice_kb2040:
	$(call build_app,controller_fisherprice_kb2040)

.PHONY: controller_fisherprice_analog_kb2040
controller_fisherprice_analog_kb2040:
	$(call build_app,controller_fisherprice_analog_kb2040)

.PHONY: controller_alpakka_pico
controller_alpakka_pico:
	$(call build_app,controller_alpakka_pico)

.PHONY: controller_macropad
controller_macropad:
	$(call build_app,controller_macropad)

# Console-only targets (defaults to KB2040)
.PHONY: 3do
3do:
	@echo "$(YELLOW)Building 3DO (KB2040)...$(NC)"
	@cd src && rm -rf build && sh $(BOARD_SCRIPT_kb2040)
	@cd src/build && $(MAKE) --no-print-directory $(CONSOLE_3do) -j4
	@echo "$(GREEN)✓ 3DO built successfully$(NC)"
	@echo "  Output: src/build/$(CONSOLE_3do).uf2"
	@echo ""

.PHONY: pce
pce:
	@echo "$(YELLOW)Building PCEngine (KB2040)...$(NC)"
	@cd src && rm -rf build && sh $(BOARD_SCRIPT_kb2040)
	@cd src/build && $(MAKE) --no-print-directory $(CONSOLE_pce) -j4
	@echo "$(GREEN)✓ PCEngine built successfully$(NC)"
	@echo "  Output: src/build/$(CONSOLE_pce).uf2"
	@echo ""

.PHONY: ngc
ngc:
	@echo "$(YELLOW)Building GameCube (KB2040)...$(NC)"
	@cd src && rm -rf build && sh $(BOARD_SCRIPT_kb2040)
	@cd src/build && $(MAKE) --no-print-directory $(CONSOLE_ngc) -j4
	@echo "$(GREEN)✓ GameCube built successfully$(NC)"
	@echo "  Output: src/build/$(CONSOLE_ngc).uf2"
	@echo ""

.PHONY: ngc_rp2040zero
ngc_rp2040zero:
	@echo "$(YELLOW)Building GameCube (RP2040-Zero)...$(NC)"
	@cd src && rm -rf build && sh $(BOARD_SCRIPT_rp2040zero)
	@cd src/build && $(MAKE) --no-print-directory $(CONSOLE_ngc) -j4
	@echo "$(GREEN)✓ GameCube built successfully$(NC)"
	@echo "  Output: src/build/$(CONSOLE_ngc).uf2"
	@echo ""

.PHONY: nuon
nuon:
	@echo "$(YELLOW)Building Nuon (KB2040)...$(NC)"
	@cd src && rm -rf build && sh $(BOARD_SCRIPT_kb2040)
	@cd src/build && $(MAKE) --no-print-directory $(CONSOLE_nuon) -j4
	@echo "$(GREEN)✓ Nuon built successfully$(NC)"
	@echo "  Output: src/build/$(CONSOLE_nuon).uf2"
	@echo ""

.PHONY: loopy
loopy:
	@echo "$(YELLOW)Building Loopy (KB2040)...$(NC)"
	@cd src && rm -rf build && sh $(BOARD_SCRIPT_kb2040)
	@cd src/build && $(MAKE) --no-print-directory $(CONSOLE_loopy) -j4
	@echo "$(GREEN)✓ Loopy built successfully$(NC)"
	@echo "  Output: src/build/$(CONSOLE_loopy).uf2"
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

.PHONY: flash-usb2nuon_kb2040
flash-usb2nuon_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2nuon_kb2040

.PHONY: flash-usb2loopy_kb2040
flash-usb2loopy_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2loopy_kb2040

.PHONY: flash-usb2dc_kb2040
flash-usb2dc_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2dc_kb2040

.PHONY: flash-n642dc_kb2040
flash-n642dc_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=n642dc_kb2040

.PHONY: flash-usb23do_rp2040zero
flash-usb23do_rp2040zero:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb23do_rp2040zero

.PHONY: flash-snes23do_rp2040zero
flash-snes23do_rp2040zero:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=snes23do_rp2040zero

.PHONY: flash-usb2uart_kb2040
flash-usb2uart_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2uart_kb2040

.PHONY: flash-usb2usb_feather
flash-usb2usb_feather:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2usb_feather

.PHONY: flash-usb2usb_rp2040zero
flash-usb2usb_rp2040zero:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2usb_rp2040zero

.PHONY: flash-usb2usb_rp2350usba
flash-usb2usb_rp2350usba:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2usb_rp2350usba

.PHONY: flash-bt2usb_pico_w
flash-bt2usb_pico_w:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=bt2usb_pico_w

.PHONY: flash-snes2usb_kb2040
flash-snes2usb_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=snes2usb_kb2040

.PHONY: flash-controller_fisherprice_kb2040
flash-controller_fisherprice_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=controller_fisherprice_kb2040

.PHONY: flash-controller_fisherprice_analog_kb2040
flash-controller_fisherprice_analog_kb2040:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=controller_fisherprice_analog_kb2040

.PHONY: flash-controller_alpakka_pico
flash-controller_alpakka_pico:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=controller_alpakka_pico

.PHONY: flash-controller_macropad
flash-controller_macropad:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=controller_macropad

# Internal flash helper for specific app (finds most recent matching file)
.PHONY: _flash_app
_flash_app:
	@# Determine correct volume based on app name (RP2350 boards use different volume)
	@if echo "$(APP_NAME)" | grep -q "rp2350"; then \
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

# Show current configuration
.PHONY: config
config:
	@echo "$(BLUE)Current Configuration:$(NC)"
	@echo "  PICO_SDK_PATH:       $(PICO_SDK_PATH)"
	@echo "  PICO_TOOLCHAIN_PATH: $(PICO_TOOLCHAIN_PATH)"
	@echo ""
