# Joypad ESP32-S3 Build System
# Board selection via BOARD variable (default: devkit)
# App selection via CONFIG_APP variable (default: bt2usb)

.PHONY: init build flash monitor clean menuconfig help uf2

# Default board and app
BOARD ?= devkit
CONFIG_APP ?= bt2usb

# Auto-source ESP-IDF environment
IDF_ENV := . ./env.sh >/dev/null 2>&1 &&

# Pass CONFIG_APP and BOARD to CMake
CMAKE_EXTRA_ARGS := -DCONFIG_APP=$(CONFIG_APP) -DCONFIG_BOARD=$(BOARD)

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
	@echo "Building $(CONFIG_APP) for ESP32-S3 ($(BOARD))"
	$(IDF_ENV) SDKCONFIG_DEFAULTS="$(SDKCONFIG_FILES)" idf.py $(CMAKE_EXTRA_ARGS) build

# --- UF2 / Flash variables ---

UF2CONV := tools/uf2conv.py
UF2_FAMILY := 0xc47e5767
UF2_VOLUME := /Volumes/FTHRS3BOOT
# ESP-IDF project name matches CONFIG_APP (from CMakeLists.txt)
BUILD_APP := build/joypad_$(CONFIG_APP).bin
BUILD_UF2 := build/joypad_$(CONFIG_APP).uf2

# Generate .uf2 from built app binary.
# Base address 0 = offset within ota_0 partition (TinyUF2 convention).
uf2: build
	@echo "Generating UF2 file..."
	python3 $(UF2CONV) $(BUILD_APP) \
		--base 0 \
		--family $(UF2_FAMILY) \
		--output $(BUILD_UF2) \
		--convert
	@echo "UF2 file: $(BUILD_UF2)"

# Flash via TinyUF2 drag-and-drop.
# Double-tap reset to enter TinyUF2 bootloader before running.
flash: uf2
	@if [ ! -d "$(UF2_VOLUME)" ]; then \
		echo "ERROR: TinyUF2 volume not found at $(UF2_VOLUME)"; \
		echo "Double-tap the RESET button to enter TinyUF2 bootloader."; \
		exit 1; \
	fi
	@echo "Copying $(BUILD_UF2) to $(UF2_VOLUME)..."
	@cp $(BUILD_UF2) $(UF2_VOLUME)/ 2>/dev/null; true
	@echo "Done! Board will reboot automatically."

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
	@echo "  make flash [CONFIG_APP=...] - Build + flash via TinyUF2 (double-tap reset first)"
	@echo "  make uf2 [CONFIG_APP=...]   - Build + generate .uf2 file"
	@echo "  make monitor                - Monitor serial output"
	@echo "  make clean                  - Clean build files"
	@echo "  make menuconfig             - Open ESP-IDF config menu"
	@echo ""
	@echo "First-time setup:"
	@echo "  1. Flash TinyUF2 release for your board (from github.com/adafruit/tinyuf2)"
	@echo "  2. Double-tap RESET, then: make flash"
	@echo ""
	@echo "Add new boards:"
	@echo "  1. Create sdkconfig.board.<name> with board-specific overrides"
	@echo "  2. Run: make BOARD=<name>"
	@echo ""
	@echo "Setup:"
	@echo "  make init            - Install ESP-IDF v6.0 and tools"
	@echo ""
