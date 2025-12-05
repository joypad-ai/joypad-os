# USBRetro Top-Level Makefile
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
BOARD_SCRIPT_kb2040 := boards/build_ada_kb2040.sh
BOARD_SCRIPT_qtpy := boards/build_ada_qtpy.sh
BOARD_SCRIPT_rp2040zero := boards/build_waveshare_rp2040_zero.sh
BOARD_SCRIPT_feather_usbhost := boards/build_ada_feather_usbhost.sh
BOARD_SCRIPT_macropad := boards/build_ada_macropad.sh

# Console targets (cmake target names)
CONSOLE_3do := usbretro_3do
CONSOLE_pce := usbretro_pce
CONSOLE_ngc := usbretro_ngc
CONSOLE_nuon := usbretro_nuon
CONSOLE_loopy := usbretro_loopy
CONSOLE_snes3do := usbretro_snes3do
CONSOLE_uart := usbretro_uart
CONSOLE_usb := usbretro_usb
CONSOLE_usb_rp2040zero := usbretro_usb_rp2040zero
CONSOLE_snes2usb := usbretro_snes2usb
CONSOLE_controller_fisherprice := usbretro_controller_fisherprice
CONSOLE_controller_fisherprice_analog := usbretro_controller_fisherprice_analog
CONSOLE_controller_alpakka := usbretro_controller_alpakka
CONSOLE_controller_macropad := usbretro_controller_macropad

# App definitions: APP_name = board console output_name
# Naming convention: usb2<console> for all apps
APP_usb2pce := kb2040 pce usb2pce
APP_usb2gc := kb2040 ngc usb2gc
APP_usb2nuon := kb2040 nuon usb2nuon
APP_usb2loopy := kb2040 loopy usb2loopy
APP_usb23do := rp2040zero 3do usb23do
APP_snes23do := rp2040zero snes3do snes23do
APP_usb2uart := kb2040 uart usb2uart
APP_usb2usb := feather_usbhost usb usb2usb
APP_usb2usb_rp2040zero := rp2040zero usb_rp2040zero usb2usb_rp2040zero
APP_snes2usb := kb2040 snes2usb snes2usb
APP_controller_fisherprice := kb2040 controller_fisherprice controller_fisherprice
APP_controller_fisherprice_analog := kb2040 controller_fisherprice_analog controller_fisherprice_analog
APP_controller_alpakka := pico controller_alpakka controller_alpakka
APP_controller_macropad := macropad controller_macropad controller_macropad

# All apps (note: controller_macropad not included - build explicitly with 'make controller_macropad')
APPS := usb2pce usb2gc usb2nuon usb2loopy usb23do snes23do usb2uart usb2usb usb2usb_rp2040zero snes2usb controller_fisherprice controller_alpakka

# Stable apps for release (mature enough for public release)
RELEASE_APPS := usb2pce usb2gc usb2nuon

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
	@echo "$(BLUE)USBRetro Firmware Build System$(NC)"
	@echo "$(BLUE)==============================$(NC)"
	@echo ""
	@echo "$(GREEN)Quick Start:$(NC)"
	@echo "  make init          - Initialize submodules (run once after clone)"
	@echo "  make build         - Build all apps (alias for 'make all')"
	@echo ""
	@echo "$(GREEN)App Targets:$(NC)"
	@echo "  make usb2pce       - Build usb2pce (KB2040 + PCEngine)"
	@echo "  make usb2gc        - Build usb2gc (KB2040 + GameCube)"
	@echo "  make usb2nuon      - Build usb2nuon (KB2040 + Nuon)"
	@echo "  make usb2loopy     - Build usb2loopy (KB2040 + Loopy)"
	@echo "  make usb23do       - Build usb23do (RP2040 Zero + 3DO)"
	@echo "  make snes23do      - Build snes23do (RP2040 Zero + SNES->3DO)"
	@echo "  make usb2uart      - Build usb2uart (KB2040 + UART ESP32 bridge)"
	@echo "  make usb2usb       - Build usb2usb (Feather USB Host + USB HID gamepad)"
	@echo "  make usb2usb_rp2040zero - Build usb2usb for rp2040zero (RP2040-Zero + USB HID gamepad)"
	@echo "  make snes2usb      - Build snes2usb (KB2040 + SNES→USB HID gamepad)"
	@echo "  make controller_fisherprice - Build controller_fisherprice (KB2040 + GPIO→USB HID gamepad)"
	@echo "  make controller_fisherprice_analog - Build controller_fisherprice_analog (KB2040 + ADC analog stick)"
	@echo "  make controller_alpakka - Build controller_alpakka (Pico + GPIO/I2C→USB HID gamepad)"
	@echo "  make controller_macropad - Build controller_macropad (MacroPad RP2040 + 12 keys→USB HID gamepad)"
	@echo ""
	@echo "$(GREEN)Convenience Targets:$(NC)"
	@echo "  make all           - Build all apps"
	@echo "  make clean         - Clean build artifacts"
	@echo "  make fullclean     - Reset to fresh clone state (removes all untracked files)"
	@echo "  make releases      - Build stable apps for release"
	@echo ""
	@echo "$(GREEN)Flash Targets:$(NC)"
	@echo "  make flash         - Flash most recently built firmware"
	@echo "  make flash-usb2pce - Flash usb2pce firmware"
	@echo "  make flash-usb2gc  - Flash usb2gc firmware"
	@echo "  make flash-usb2nuon - Flash usb2nuon firmware"
	@echo "  make flash-usb2loopy - Flash usb2loopy firmware"
	@echo "  make flash-usb23do - Flash usb23do firmware"
	@echo "  make flash-snes23do - Flash snes23do firmware"
	@echo "  make flash-usb2uart - Flash usb2uart firmware"
	@echo "  make flash-usb2usb - Flash usb2usb firmware"
	@echo "  make flash-usb2usb_rp2040zero - Flash usb2usb_rp2040zero firmware"
	@echo "  make flash-snes2usb - Flash snes2usb firmware"
	@echo "  make flash-controller_fisherprice - Flash controller_fisherprice firmware"
	@echo "  make flash-controller_fisherprice_analog - Flash controller_fisherprice_analog firmware"
	@echo "  make flash-controller_alpakka - Flash controller_alpakka firmware"
	@echo "  make flash-controller_macropad - Flash controller_macropad firmware"
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
# Output naming: usbr_<version|commit>_<board>_<app>.uf2
define build_app
	@echo "$(YELLOW)Building $1...$(NC)"
	@echo "  Board:   $(word 1,$(APP_$1))"
	@echo "  Console: $(word 2,$(APP_$1))"
	@echo "  Version: $(VERSION_ID)"
	@cd src && rm -rf build
	@cd src && sh $(BOARD_SCRIPT_$(word 1,$(APP_$1)))
	@cd src/build && $(MAKE) --no-print-directory $(CONSOLE_$(word 2,$(APP_$1))) -j4
	@mkdir -p $(RELEASE_DIR)
	@cp src/build/$(CONSOLE_$(word 2,$(APP_$1))).uf2 \
	    $(RELEASE_DIR)/usbr_$(VERSION_ID)_$(word 1,$(APP_$1))_$(word 3,$(APP_$1)).uf2
	@echo "$(GREEN)✓ $1 built successfully$(NC)"
	@echo "  Output: $(RELEASE_DIR)/usbr_$(VERSION_ID)_$(word 1,$(APP_$1))_$(word 3,$(APP_$1)).uf2"
	@echo ""
endef

# App targets
.PHONY: usb2pce
usb2pce:
	$(call build_app,usb2pce)

.PHONY: usb2gc
usb2gc:
	$(call build_app,usb2gc)

.PHONY: usb2nuon
usb2nuon:
	$(call build_app,usb2nuon)

.PHONY: usb2loopy
usb2loopy:
	$(call build_app,usb2loopy)

.PHONY: usb23do
usb23do:
	$(call build_app,usb23do)

.PHONY: snes23do
snes23do:
	$(call build_app,snes23do)

.PHONY: usb2uart
usb2uart:
	$(call build_app,usb2uart)

.PHONY: usb2usb
usb2usb:
	$(call build_app,usb2usb)

.PHONY: usb2usb_rp2040zero
usb2usb_rp2040zero:
	$(call build_app,usb2usb_rp2040zero)

.PHONY: snes2usb
snes2usb:
	$(call build_app,snes2usb)

.PHONY: controller_fisherprice
controller_fisherprice:
	$(call build_app,controller_fisherprice)

.PHONY: controller_fisherprice_analog
controller_fisherprice_analog:
	$(call build_app,controller_fisherprice_analog)

.PHONY: controller_alpakka
controller_alpakka:
	$(call build_app,controller_alpakka)

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
	@if [ ! -d "/Volumes/RPI-RP2" ]; then \
		echo "$(YELLOW)⚠ RPI-RP2 drive not found at /Volumes/RPI-RP2$(NC)"; \
		echo "$(YELLOW)  Please put device in bootloader mode:$(NC)"; \
		echo "$(YELLOW)  - USB2PCE/GCUSB: Hold BOOT button while plugging in USB-C$(NC)"; \
		echo "$(YELLOW)  - Or unplug all USB devices and plug in USB-C$(NC)"; \
		exit 1; \
	fi
	@LATEST_UF2=$$(ls -t $(RELEASE_DIR)/*.uf2 2>/dev/null | head -1); \
	if [ -z "$$LATEST_UF2" ]; then \
		echo "$(YELLOW)⚠ No UF2 files found in $(RELEASE_DIR)$(NC)"; \
		echo "$(YELLOW)  Run 'make usb2pce' or another build target first$(NC)"; \
		exit 1; \
	fi; \
	echo "$(YELLOW)Flashing $$(basename $$LATEST_UF2)...$(NC)"; \
	cp "$$LATEST_UF2" /Volumes/RPI-RP2/ && \
	echo "$(GREEN)✓ Firmware flashed successfully!$(NC)" && \
	echo "$(GREEN)  Device will reboot automatically$(NC)"

# Flash specific apps (finds most recent matching file)
.PHONY: flash-usb2pce
flash-usb2pce:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2pce

.PHONY: flash-usb2gc
flash-usb2gc:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2gc

.PHONY: flash-usb2nuon
flash-usb2nuon:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2nuon

.PHONY: flash-usb2loopy
flash-usb2loopy:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2loopy

.PHONY: flash-usb23do
flash-usb23do:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb23do

.PHONY: flash-snes23do
flash-snes23do:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=snes23do

.PHONY: flash-usb2uart
flash-usb2uart:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2uart

.PHONY: flash-usb2usb
flash-usb2usb:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2usb

.PHONY: flash-usb2usb_rp2040zero
flash-usb2usb_rp2040zero:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=usb2usb_rp2040zero

.PHONY: flash-snes2usb
flash-snes2usb:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=snes2usb

.PHONY: flash-controller_fisherprice
flash-controller_fisherprice:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=controller_fisherprice

.PHONY: flash-controller_fisherprice_analog
flash-controller_fisherprice_analog:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=controller_fisherprice_analog

.PHONY: flash-controller_alpakka
flash-controller_alpakka:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=controller_alpakka

.PHONY: flash-controller_macropad
flash-controller_macropad:
	@$(MAKE) --no-print-directory _flash_app APP_NAME=controller_macropad

# Internal flash helper for specific app (finds most recent matching file)
.PHONY: _flash_app
_flash_app:
	@if [ ! -d "/Volumes/RPI-RP2" ]; then \
		echo "$(YELLOW)⚠ RPI-RP2 drive not found at /Volumes/RPI-RP2$(NC)"; \
		echo "$(YELLOW)  Please put device in bootloader mode$(NC)"; \
		exit 1; \
	fi
	@FLASH_FILE=$$(ls -t $(RELEASE_DIR)/usbr_*_$(APP_NAME).uf2 2>/dev/null | head -1); \
	if [ -z "$$FLASH_FILE" ]; then \
		echo "$(YELLOW)⚠ No $(APP_NAME) firmware found in $(RELEASE_DIR)$(NC)"; \
		echo "$(YELLOW)  Build it first with 'make $(APP_NAME)'$(NC)"; \
		exit 1; \
	fi; \
	echo "$(YELLOW)Flashing $$(basename $$FLASH_FILE)...$(NC)"; \
	cp "$$FLASH_FILE" /Volumes/RPI-RP2/ && \
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
