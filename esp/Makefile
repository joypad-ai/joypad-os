# Joypad ESP32-S3 bt2usb Build System
# Board selection via BOARD variable (default: devkit)

.PHONY: init build flash monitor clean menuconfig help

# Default board
BOARD ?= devkit

# Auto-source ESP-IDF environment
IDF_ENV := . ./env.sh >/dev/null 2>&1 &&

# Board configurations
COMMON_SDKCONFIG := sdkconfig.defaults

SDKCONFIG_BOARD := sdkconfig.board.$(BOARD)
SDKCONFIG_FILES := $(COMMON_SDKCONFIG)
ifneq ($(wildcard $(SDKCONFIG_BOARD)),)
    SDKCONFIG_FILES := $(COMMON_SDKCONFIG);$(SDKCONFIG_BOARD)
endif

init:
	@if [ ! -d "$(HOME)/esp-idf" ]; then \
		echo "Cloning ESP-IDF v6.0..."; \
		git clone --branch v6.0 --depth 1 --recursive https://github.com/espressif/esp-idf.git $(HOME)/esp-idf; \
	else \
		echo "ESP-IDF already installed at ~/esp-idf"; \
	fi
	@cd $(HOME)/esp-idf && ./install.sh esp32s3
	@echo "ESP-IDF setup complete. Run 'source env.sh' to activate."

build:
	@echo "Building bt2usb for ESP32-S3 ($(BOARD))"
	$(IDF_ENV) SDKCONFIG_DEFAULTS="$(SDKCONFIG_FILES)" idf.py build

flash:
	@echo "Flashing bt2usb to ESP32-S3 ($(BOARD))"
	$(IDF_ENV) SDKCONFIG_DEFAULTS="$(SDKCONFIG_FILES)" idf.py flash

monitor:
	@echo "Opening serial monitor"
	@echo "NOTE: Use Ctrl+] to exit"
	$(IDF_ENV) idf.py monitor

clean:
	$(IDF_ENV) idf.py fullclean

menuconfig:
	$(IDF_ENV) SDKCONFIG_DEFAULTS="$(SDKCONFIG_FILES)" idf.py menuconfig

# Board shortcuts
devkit:
	@$(MAKE) BOARD=devkit build

# Help
help:
	@echo "Joypad ESP32-S3 Build Commands:"
	@echo ""
	@echo "  make [BOARD=devkit]  - Build for specified board (default: devkit)"
	@echo "  make devkit          - Build for ESP32-S3 DevKit"
	@echo ""
	@echo "  make flash [BOARD=...] - Flash firmware"
	@echo "  make monitor           - Monitor serial output"
	@echo "  make clean             - Clean build files"
	@echo "  make menuconfig        - Open ESP-IDF config menu"
	@echo ""
	@echo "Add new boards:"
	@echo "  1. Create sdkconfig.board.<name> with board-specific overrides"
	@echo "  2. Run: make BOARD=<name>"
	@echo ""
	@echo "Setup:"
	@echo "  make init            - Install ESP-IDF v6.0 and tools"
	@echo ""
	@echo "Prerequisites:"
	@echo "  Run 'make init' or install ESP-IDF v6.0+ at ~/esp-idf manually"
	@echo ""
