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

# Console targets (cmake target names)
CONSOLE_3do := usbretro_3do
CONSOLE_pce := usbretro_pce
CONSOLE_ngc := usbretro_ngc
CONSOLE_xb1 := usbretro_xb1
CONSOLE_nuon := usbretro_nuon
CONSOLE_loopy := usbretro_loopy
CONSOLE_snes3do := usbretro_snes3do

# App definitions: APP_name = board console output_name
# Naming convention: usb2<console> for all apps
APP_usb2pce := kb2040 pce usb2pce
APP_usb2gc := kb2040 ngc usb2gc
APP_usb2nuon := kb2040 nuon usb2nuon
APP_usb2xb1 := qtpy xb1 usb2xb1
APP_usb2loopy := kb2040 loopy usb2loopy
APP_usb23do := rp2040zero 3do usb23do
APP_snes23do := rp2040zero snes3do snes23do

# All apps
APPS := usb2pce usb2gc usb2nuon usb2xb1 usb2loopy usb23do snes23do

# Stable apps for release (mature enough for public release)
RELEASE_APPS := usb2pce usb2gc usb2nuon

# Release directory
RELEASE_DIR := releases

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
	@echo "  make usb2xb1       - Build usb2xb1 (QT Py + Xbox One)"
	@echo "  make usb2loopy     - Build usb2loopy (KB2040 + Loopy)"
	@echo "  make usb23do       - Build usb23do (RP2040 Zero + 3DO)"
	@echo "  make snes23do      - Build snes23do (RP2040 Zero + SNES->3DO)"
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
	@echo "  make flash-usb2xb1 - Flash usb2xb1 firmware"
	@echo "  make flash-usb2loopy - Flash usb2loopy firmware"
	@echo "  make flash-usb23do - Flash usb23do firmware"
	@echo "  make flash-snes23do - Flash snes23do firmware"
	@echo ""
	@echo "$(GREEN)Console-Only Targets (uses KB2040):$(NC)"
	@echo "  make pce           - Build PCEngine firmware"
	@echo "  make ngc           - Build GameCube firmware"
	@echo "  make nuon          - Build Nuon firmware"
	@echo "  make xb1           - Build Xbox One firmware"
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
define build_app
	@echo "$(YELLOW)Building $1...$(NC)"
	@echo "  Board:   $(word 1,$(APP_$1))"
	@echo "  Console: $(word 2,$(APP_$1))"
	@cd src && rm -rf build
	@cd src && sh $(BOARD_SCRIPT_$(word 1,$(APP_$1)))
	@cd src/build && $(MAKE) --no-print-directory $(CONSOLE_$(word 2,$(APP_$1))) -j4
	@mkdir -p $(RELEASE_DIR)
	@cp src/build/$(CONSOLE_$(word 2,$(APP_$1))).uf2 \
	    $(RELEASE_DIR)/$(word 3,$(APP_$1))_$(CONSOLE_$(word 2,$(APP_$1))).uf2
	@echo "$(GREEN)✓ $1 built successfully$(NC)"
	@echo "  Output: $(RELEASE_DIR)/$(word 3,$(APP_$1))_$(CONSOLE_$(word 2,$(APP_$1))).uf2"
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

.PHONY: usb2xb1
usb2xb1:
	$(call build_app,usb2xb1)

.PHONY: usb2loopy
usb2loopy:
	$(call build_app,usb2loopy)

.PHONY: usb23do
usb23do:
	$(call build_app,usb23do)

.PHONY: snes23do
snes23do:
	$(call build_app,snes23do)

# Legacy aliases for backward compatibility
.PHONY: gcusb nuonusb xboxadapter 3dousb
gcusb: usb2gc
nuonusb: usb2nuon
xboxadapter: usb2xb1
3dousb: usb23do

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

.PHONY: xb1
xb1:
	@echo "$(YELLOW)Building Xbox One (QT Py)...$(NC)"
	@cd src && rm -rf build && sh $(BOARD_SCRIPT_qtpy)
	@cd src/build && $(MAKE) --no-print-directory $(CONSOLE_xb1) -j4
	@echo "$(GREEN)✓ Xbox One built successfully$(NC)"
	@echo "  Output: src/build/$(CONSOLE_xb1).uf2"
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

# Flash specific apps
.PHONY: flash-usb2pce
flash-usb2pce:
	@$(MAKE) --no-print-directory _flash FLASH_FILE=$(RELEASE_DIR)/$(word 3,$(APP_usb2pce))_$(CONSOLE_$(word 2,$(APP_usb2pce))).uf2

.PHONY: flash-usb2gc
flash-usb2gc:
	@$(MAKE) --no-print-directory _flash FLASH_FILE=$(RELEASE_DIR)/$(word 3,$(APP_usb2gc))_$(CONSOLE_$(word 2,$(APP_usb2gc))).uf2

.PHONY: flash-usb2nuon
flash-usb2nuon:
	@$(MAKE) --no-print-directory _flash FLASH_FILE=$(RELEASE_DIR)/$(word 3,$(APP_usb2nuon))_$(CONSOLE_$(word 2,$(APP_usb2nuon))).uf2

.PHONY: flash-usb2xb1
flash-usb2xb1:
	@$(MAKE) --no-print-directory _flash FLASH_FILE=$(RELEASE_DIR)/$(word 3,$(APP_usb2xb1))_$(CONSOLE_$(word 2,$(APP_usb2xb1))).uf2

.PHONY: flash-usb2loopy
flash-usb2loopy:
	@$(MAKE) --no-print-directory _flash FLASH_FILE=$(RELEASE_DIR)/$(word 3,$(APP_usb2loopy))_$(CONSOLE_$(word 2,$(APP_usb2loopy))).uf2

.PHONY: flash-usb23do
flash-usb23do:
	@$(MAKE) --no-print-directory _flash FLASH_FILE=$(RELEASE_DIR)/$(word 3,$(APP_usb23do))_$(CONSOLE_$(word 2,$(APP_usb23do))).uf2

.PHONY: flash-snes23do
flash-snes23do:
	@$(MAKE) --no-print-directory _flash FLASH_FILE=$(RELEASE_DIR)/$(word 3,$(APP_snes23do))_$(CONSOLE_$(word 2,$(APP_snes23do))).uf2

# Legacy flash aliases
.PHONY: flash-gcusb flash-nuonusb flash-xboxadapter flash-3dousb
flash-gcusb: flash-usb2gc
flash-nuonusb: flash-usb2nuon
flash-xboxadapter: flash-usb2xb1
flash-3dousb: flash-usb23do

# Internal flash helper
.PHONY: _flash
_flash:
	@if [ ! -d "/Volumes/RPI-RP2" ]; then \
		echo "$(YELLOW)⚠ RPI-RP2 drive not found at /Volumes/RPI-RP2$(NC)"; \
		echo "$(YELLOW)  Please put device in bootloader mode$(NC)"; \
		exit 1; \
	fi
	@if [ ! -f "$(FLASH_FILE)" ]; then \
		echo "$(YELLOW)⚠ File not found: $(FLASH_FILE)$(NC)"; \
		echo "$(YELLOW)  Build the firmware first$(NC)"; \
		exit 1; \
	fi
	@echo "$(YELLOW)Flashing $$(basename $(FLASH_FILE))...$(NC)"
	@cp "$(FLASH_FILE)" /Volumes/RPI-RP2/
	@echo "$(GREEN)✓ Firmware flashed successfully!$(NC)"
	@echo "$(GREEN)  Device will reboot automatically$(NC)"

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
