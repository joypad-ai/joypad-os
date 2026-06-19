# Makefile for Joypad on nRF52840 boards (nRF Connect SDK / Zephyr)
#
# Supported boards: xiao_ble, adafruit_feather_nrf52840
# Supported apps:   bt2usb (default), usb2usb (with MAX3421E FeatherWing),
#                   btusb2usb (MAX3421E + Bluetooth combined),
#                   controller_btusb (sensor input + BLE peripheral + USB)
# Requires: nRF Connect SDK v3.1.0 (installed via 'make init')
# Usage:
#   make init              # One-time workspace setup
#   make build             # Build for default board (xiao_ble)
#   make build BOARD=adafruit_feather_nrf52840  # Build for Feather
#   make build APP_TYPE=usb2usb BOARD=adafruit_feather_nrf52840  # usb2usb
#   make build APP_TYPE=btusb2usb BOARD=adafruit_feather_nrf52840  # btusb2usb
#   make flash-uf2         # Flash via UF2 bootloader drive
#   make clean             # Remove build directory

.PHONY: init build flash-uf2 flash-jlink monitor clean fullclean help

BOARD ?= xiao_ble
APP_TYPE ?= bt2usb

# Map JoypadOS board aliases -> actual Zephyr board. Overlay/conf are keyed off
# the alias (BOARD), but west builds against ZEPHYR_BOARD. The April Brother
# dongle is the upstream nrf52840dongle_nrf52840 board + a custom overlay.
ZEPHYR_BOARD := $(BOARD)
ifeq ($(BOARD),aprbrother_nrf52840)
    ZEPHYR_BOARD := nrf52840dongle_nrf52840
endif

# Auto-detect NCS toolchain (adds west, cmake, python3, etc. to PATH)
NCS_TOOLCHAIN := $(lastword $(wildcard /opt/nordic/ncs/toolchains/*/bin))
ifneq ($(NCS_TOOLCHAIN),)
    export PATH := $(NCS_TOOLCHAIN):$(PATH)
endif

# Build with board-specific overlay and conf if they exist
OVERLAY := $(wildcard $(shell pwd)/boards/$(BOARD).overlay)
CONF := $(wildcard $(shell pwd)/boards/$(BOARD).conf)

# App-specific extra conf (e.g., prj_usb2usb.conf)
APP_CONF := $(wildcard $(shell pwd)/prj_$(APP_TYPE).conf)

BUILD_ARGS := west build -b $(ZEPHYR_BOARD) . --pristine=always --
BUILD_ARGS += -DAPP_TYPE=$(APP_TYPE)
ifneq ($(OVERLAY),)
    BUILD_ARGS += -DDTC_OVERLAY_FILE="$(OVERLAY)"
endif

# Combine board conf and app conf into EXTRA_CONF_FILE (semicolon-separated for CMake)
EXTRA_CONFS :=
ifneq ($(CONF),)
    EXTRA_CONFS := $(CONF)
endif
ifneq ($(APP_CONF),)
    ifneq ($(EXTRA_CONFS),)
        EXTRA_CONFS := $(EXTRA_CONFS);$(APP_CONF)
    else
        EXTRA_CONFS := $(APP_CONF)
    endif
endif
ifneq ($(EXTRA_CONFS),)
    BUILD_ARGS += -DEXTRA_CONF_FILE="$(EXTRA_CONFS)"
endif

# Initialize nRF Connect SDK workspace (one-time setup)
init:
	@if [ -d ".west" ]; then \
		echo "Workspace already initialized. Run 'make fullclean' first to reinitialize."; \
	else \
		echo "Initializing nRF Connect SDK v3.1.0 workspace..."; \
		west init -m https://github.com/nrfconnect/sdk-nrf.git --mr v3.1.0; \
		west update --narrow -o=--depth=1; \
		echo "Workspace initialized."; \
	fi

# Build the project
build:
	@echo "Building $(APP_TYPE) for nRF52840 (board=$(BOARD))"
	$(BUILD_ARGS)

# Flash via UF2 bootloader (double-tap reset to enter bootloader)
flash-uf2:
	@UF2="build/nrf/zephyr/zephyr.uf2"; \
	if [ ! -f "$$UF2" ]; then \
		echo "Error: No UF2 file found. Run 'make build' first."; \
		exit 1; \
	fi; \
	for DRIVE in XIAO-SENSE NRF52BOOT FTHR840BOOT MDBT50QBOOT UF2BOOT; do \
		if [ -d "/Volumes/$$DRIVE" ]; then \
			echo "Copying UF2 to /Volumes/$$DRIVE..."; \
			cp "$$UF2" "/Volumes/$$DRIVE/"; \
			echo "Flash complete! Device will reset automatically."; \
			exit 0; \
		fi; \
	done; \
	echo "Error: No UF2 bootloader drive found."; \
	echo "Enter bootloader mode (double-tap reset) and try again."; \
	echo "Expected drives: XIAO-SENSE, NRF52BOOT, FTHR840BOOT, MDBT50QBOOT, UF2BOOT"; \
	exit 1

# Flash via J-Link debugger
flash-jlink:
	west flash --runner jlink

# Monitor via USB CDC serial
monitor:
	@echo "Opening serial monitor (Ctrl+C to exit)"
	@PORT=$$(ls /dev/cu.usbmodem* 2>/dev/null | head -1); \
	if [ -n "$$PORT" ]; then \
		echo "Auto-detected port: $$PORT"; \
		python3 -m serial.tools.miniterm $$PORT 115200 --eol LF --raw; \
	else \
		echo "No USB serial port found."; \
	fi

# Clean build directory
clean:
	rm -rf build

# Full workspace cleanup (requires re-initialization)
fullclean:
	rm -rf .west nrf zephyr nrfxlib modules bootloader test tools build
	@echo "Full workspace cleanup complete. Run 'make init' to reinitialize."

help:
	@echo "Available targets:"
	@echo "  init         - Initialize nRF Connect SDK workspace (one-time)"
	@echo "  build        - Build for default board (xiao_ble, override with BOARD=)"
	@echo "  flash-uf2    - Flash via UF2 bootloader drive"
	@echo "  flash-jlink  - Flash via J-Link debugger"
	@echo "  monitor      - Monitor USB CDC serial output"
	@echo "  clean        - Remove build directory"
	@echo "  fullclean    - Remove entire workspace (requires re-init)"
	@echo ""
	@echo "Variables:"
	@echo "  BOARD        - Target board (default: xiao_ble)"
	@echo "  APP_TYPE     - App to build: bt2usb (default), usb2usb, btusb2usb, or controller_btusb"
