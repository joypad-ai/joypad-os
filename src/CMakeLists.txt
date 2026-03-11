cmake_minimum_required(VERSION 3.12)

# Pull in SDK from local submodule (must be before project)
set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/pico-sdk)
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(joypad C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

set(FAMILY rp2040)

# Enable TinyUSB debug logging if JOYPAD_DEBUG is set (via .env or environment)
if(DEFINED ENV{JOYPAD_DEBUG})
    add_compile_definitions(CFG_TUSB_DEBUG=$ENV{JOYPAD_DEBUG})
endif()

# ============================================================================
# VERSION AND BUILD INFO
# ============================================================================

# Read version from VERSION file
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" JOYPAD_VERSION)
string(STRIP "${JOYPAD_VERSION}" JOYPAD_VERSION)

# Get git commit hash
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_COMMIT)
    set(GIT_COMMIT "unknown")
endif()

# Get build timestamp
string(TIMESTAMP BUILD_TIME "%Y-%m-%d %H:%M:%S")

message(STATUS "Joypad Version: ${JOYPAD_VERSION}")
message(STATUS "Git Commit: ${GIT_COMMIT}")
message(STATUS "Build Time: ${BUILD_TIME}")

# External libraries
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lib/tusb_xinput xinput_host)

# Pico-PIO-USB for dual USB (host on PIO, device on native)
set(PICO_PIO_USB_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/Pico-PIO-USB)
if(EXISTS ${PICO_PIO_USB_PATH})
    add_library(tinyusb_pico_pio_usb INTERFACE)
    target_sources(tinyusb_pico_pio_usb INTERFACE
        ${PICO_PIO_USB_PATH}/src/pio_usb.c
        ${PICO_PIO_USB_PATH}/src/pio_usb_host.c
        ${PICO_PIO_USB_PATH}/src/pio_usb_device.c
        ${PICO_PIO_USB_PATH}/src/usb_crc.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/tinyusb/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c
    )
    target_include_directories(tinyusb_pico_pio_usb INTERFACE ${PICO_PIO_USB_PATH}/src)
    target_link_libraries(tinyusb_pico_pio_usb INTERFACE hardware_pio hardware_dma pico_stdlib)
    target_compile_definitions(tinyusb_pico_pio_usb INTERFACE PIO_USB_USE_TINYUSB=1 PIO_USB_DP_PIN_DEFAULT=16)
    pico_generate_pio_header(tinyusb_pico_pio_usb ${PICO_PIO_USB_PATH}/src/usb_tx.pio)
    pico_generate_pio_header(tinyusb_pico_pio_usb ${PICO_PIO_USB_PATH}/src/usb_rx.pio)
endif()

# MAX3421E SPI USB Host (alternative to PIO USB)
add_library(tinyusb_max3421 INTERFACE)
target_sources(tinyusb_max3421 INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/lib/tinyusb/src/portable/analog/max3421/hcd_max3421.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/max3421_host.c
)
target_link_libraries(tinyusb_max3421 INTERFACE hardware_spi)
target_compile_definitions(tinyusb_max3421 INTERFACE CONFIG_MAX3421=1)

# ============================================================================
# SOURCE LISTS
# ============================================================================

# Core sources (no USB host - used by native-input apps)
set(CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/router/router.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/leds/leds.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/leds/neopixel/ws2812.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/storage/storage.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/storage/flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/button/button.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/codes/codes.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/hotkeys/hotkeys.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/players/manager.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/players/feedback.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/profiles/profile.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/profiles/profile_indicator.c
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/rp2040/platform_rp2040.c
)

# USB Host sources (HID + X-input)
set(USB_HOST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/usbh.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/hid.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/hid_utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/hid_registry.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/generic/hid_keyboard.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/generic/hid_mouse.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/generic/hid_parser.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/generic/hid_gamepad.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/8bitdo/8bitdo_bta.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/8bitdo/8bitdo_m30.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/8bitdo/8bitdo_pce.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/nintendo/gamecube_adapter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/nintendo/switch_pro.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/nintendo/switch2_pro.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/sony/sony_ds3.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/sony/sony_ds4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/sony/sony_ds5.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/sony/sony_psc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/hori/hori_horipad.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/hori/hori_pokken.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/logitech/logitech_wingman.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/sega/sega_astrocity.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/google/google_stadia.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/raphnet/raphnet_pce.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/microsoft/ms_sidewinder_dualstrike.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/microsoft/ms_sidewinder_commander.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/generic/sinput_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/xinput/xinput.c
)

# Bluetooth HID device drivers (shared between USB dongle and CYW43 transports)
set(BTHID_DEVICE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/bthid.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/bthid_registry.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/generic/bthid_gamepad.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/vendors/sony/ds3_bt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/vendors/sony/ds4_bt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/vendors/sony/ds5_bt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/vendors/nintendo/switch_pro_bt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/vendors/nintendo/switch2_ble.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/vendors/nintendo/wii_u_pro_bt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/vendors/nintendo/wiimote_bt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/vendors/microsoft/xbox_bt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/vendors/microsoft/xbox_ble.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/vendors/google/stadia_bt.c
)

# Bluetooth sources for USB dongle transport (requires BTstack)
set(BT_HOST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/btd/hci_transport_h2_tinyusb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/transport/bt_transport.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/transport/bt_transport_usb.c
    ${BTHID_DEVICE_SOURCES}
)

# BTstack sources
set(PICO_BTSTACK_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lib/pico-sdk/lib/btstack)
set(BTSTACK_SOURCES
    # Core
    ${PICO_BTSTACK_PATH}/src/btstack_linked_list.c
    ${PICO_BTSTACK_PATH}/src/btstack_memory.c
    ${PICO_BTSTACK_PATH}/src/btstack_memory_pool.c
    ${PICO_BTSTACK_PATH}/src/btstack_run_loop.c
    ${PICO_BTSTACK_PATH}/src/btstack_run_loop_base.c
    ${PICO_BTSTACK_PATH}/src/btstack_util.c
    ${PICO_BTSTACK_PATH}/src/btstack_tlv.c
    ${PICO_BTSTACK_PATH}/src/btstack_tlv_none.c
    ${PICO_BTSTACK_PATH}/src/btstack_crypto.c
    ${PICO_BTSTACK_PATH}/src/hci.c
    ${PICO_BTSTACK_PATH}/src/hci_cmd.c
    ${PICO_BTSTACK_PATH}/src/hci_dump.c
    ${PICO_BTSTACK_PATH}/src/hci_event.c
    ${PICO_BTSTACK_PATH}/src/hci_event_builder.c
    ${PICO_BTSTACK_PATH}/src/l2cap.c
    ${PICO_BTSTACK_PATH}/src/l2cap_signaling.c
    ${PICO_BTSTACK_PATH}/src/ad_parser.c
    # BLE
    ${PICO_BTSTACK_PATH}/src/ble/sm.c
    ${PICO_BTSTACK_PATH}/src/ble/att_dispatch.c
    ${PICO_BTSTACK_PATH}/src/ble/att_db.c
    ${PICO_BTSTACK_PATH}/src/ble/gatt_client.c
    ${PICO_BTSTACK_PATH}/src/ble/gatt_service_client.c
    ${PICO_BTSTACK_PATH}/src/ble/le_device_db_memory.c
    ${PICO_BTSTACK_PATH}/src/ble/le_device_db_tlv.c
    # BLE GATT services
    ${PICO_BTSTACK_PATH}/src/ble/gatt-service/hids_client.c
    ${PICO_BTSTACK_PATH}/src/ble/gatt-service/device_information_service_client.c
    ${PICO_BTSTACK_PATH}/src/ble/gatt-service/battery_service_client.c
    # Classic (for HID Host)
    ${PICO_BTSTACK_PATH}/src/classic/sdp_client.c
    ${PICO_BTSTACK_PATH}/src/classic/sdp_server.c
    ${PICO_BTSTACK_PATH}/src/classic/sdp_util.c
    ${PICO_BTSTACK_PATH}/src/classic/device_id_server.c
    ${PICO_BTSTACK_PATH}/src/classic/hid_host.c
    ${PICO_BTSTACK_PATH}/src/classic/btstack_link_key_db_memory.c
    ${PICO_BTSTACK_PATH}/src/classic/btstack_link_key_db_tlv.c
    # 3rd party
    ${PICO_BTSTACK_PATH}/3rd-party/micro-ecc/uECC.c
    ${PICO_BTSTACK_PATH}/3rd-party/rijndael/rijndael.c
    # Platform
    ${PICO_BTSTACK_PATH}/platform/embedded/btstack_run_loop_embedded.c
    ${PICO_BTSTACK_PATH}/platform/embedded/hci_dump_embedded_stdout.c
    ${PICO_BTSTACK_PATH}/platform/embedded/btstack_tlv_flash_bank.c
    # Pico SDK flash bank HAL (for persistent link key storage)
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/pico-sdk/src/rp2_common/pico_btstack/btstack_flash_bank.c
)

# USB Device sources (for USB output apps)
set(USB_DEVICE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/usbd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/modes/hid_mode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/modes/sinput_mode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/modes/xinput_mode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/modes/switch_mode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/modes/ps3_mode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/modes/psclassic_mode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/modes/pcemini_mode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/modes/ps4_mode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/modes/xid_mode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/modes/xbone_mode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/modes/xac_mode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/modes/kbmouse_mode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/modes/gc_adapter_mode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/cdc/cdc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/cdc/cdc_protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/cdc/cdc_commands.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/drivers/tud_xid.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/drivers/tud_xinput.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/drivers/tud_xbone.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/drivers/xgip_protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/kbmouse/kbmouse.c
    # libxsm3 - Xbox Security Method 3 (LGPL-2.1-or-later)
    # https://github.com/InvoxiPlayGames/libxsm3
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/libxsm3/xsm3.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/libxsm3/usbdsec.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/libxsm3/excrypt_des.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/libxsm3/excrypt_parve.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/libxsm3/excrypt_sha.c
)

# NES host sources
set(NES_HOST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/nes/nes_host.c
)

# SNES host sources
set(SNES_HOST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/snes/snes_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/SNESpad/src/snespad.c
)

# N64 host sources
set(N64_HOST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/n64/n64_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/src/joybus.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/src/N64Controller.c
)

# GC host sources (GameCube controller reading)
set(GC_HOST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/gc/gc_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/src/joybus.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/src/GamecubeController.c
)

# Nuon host sources (polyface master, software bit-bang)
# TODO: uncomment when nuon_host.c is implemented
# set(NUON_HOST_SOURCES
#     ${CMAKE_CURRENT_SOURCE_DIR}/native/host/nuon/nuon_host.c
# )

# I2C peer input sharing sources (STEMMA QT)
set(I2C_PEER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/i2c_peer/i2c_peer.c
)

# Controller app common sources
set(CONTROLLER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/pad/pad_input.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/speaker/speaker.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/display/display.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/display/display_spi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/uart/uart_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/uart/uart_host.c
)

# Common sources (core + USB host)
set(COMMON_SOURCES ${CORE_SOURCES} ${USB_HOST_SOURCES})

# Common libraries for USB host apps
set(COMMON_LIBRARIES pico_stdlib pico_multicore hardware_pio pico_rand tinyusb_host tinyusb_board xinput_host)

# BTstack compile definitions (shared by all USB host apps)
# Note: Most BTstack feature flags are in bt/btstack/btstack_config.h
# Only ENABLE_BTSTACK is needed here to enable CFG_TUH_BTD in tusb_config.h
set(BTSTACK_DEFINITIONS
    ENABLE_BTSTACK=1
)

# BTstack include directories
set(BTSTACK_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/btd
    ${PICO_BTSTACK_PATH}/src
    ${PICO_BTSTACK_PATH}/src/ble
    ${PICO_BTSTACK_PATH}/platform/embedded
    ${PICO_BTSTACK_PATH}/3rd-party/micro-ecc
    ${PICO_BTSTACK_PATH}/3rd-party/rijndael
    # Pico SDK BTstack flash bank HAL
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/pico-sdk/src/rp2_common/pico_btstack/include
)

# Additional BTstack sources (beyond BT_HOST_SOURCES and BTSTACK_SOURCES)
set(BTSTACK_EXTRA_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack/btstack_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack/bt_device_db.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/btd/btstack_hal.c
)

# Controller libraries
set(CONTROLLER_LIBRARIES pico_stdlib pico_multicore hardware_pio hardware_adc hardware_i2c hardware_pwm hardware_spi pico_rand tinyusb_device tinyusb_board)

# ============================================================================
# HELPER FUNCTIONS
# ============================================================================

# Common setup for all targets
function(joypad_target_common TARGET)
    target_compile_options(${TARGET} PRIVATE -O3)
    pico_add_extra_outputs(${TARGET})
    pico_generate_pio_header(${TARGET} ${CMAKE_CURRENT_LIST_DIR}/core/services/leds/neopixel/ws2812.pio)
    target_include_directories(${TARGET} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/common ${CMAKE_CURRENT_SOURCE_DIR}/lib/libxsm3)
    # Add version and build info to all targets
    target_compile_definitions(${TARGET} PRIVATE
        JOYPAD_VERSION="${JOYPAD_VERSION}"
        GIT_COMMIT="${GIT_COMMIT}"
        BUILD_TIME="${BUILD_TIME}"
        BOARD_NAME="${PICO_BOARD}"
    )
endfunction()

# Add BTstack support to a target
function(joypad_add_btstack TARGET)
    target_compile_definitions(${TARGET} PRIVATE ${BTSTACK_DEFINITIONS})
    target_sources(${TARGET} PRIVATE
        ${BT_HOST_SOURCES}
        ${BTSTACK_SOURCES}
        ${BTSTACK_EXTRA_SOURCES}
    )
    target_include_directories(${TARGET} PUBLIC ${BTSTACK_INCLUDES})
    # Link hardware_flash and pico_flash for persistent link key storage
    target_link_libraries(${TARGET} PRIVATE hardware_flash pico_flash)
endfunction()

# USB host console app (USB input -> console output)
function(joypad_console_app TARGET CONFIG_NAME APP_DIR DEVICE_DIR)
    add_executable(${TARGET})
    target_compile_definitions(${TARGET} PRIVATE CONFIG_${CONFIG_NAME}=1)
    target_sources(${TARGET} PUBLIC ${COMMON_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/native/device/${DEVICE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/${APP_DIR}/app.c
    )
    target_include_directories(${TARGET} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/${APP_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/native/device/${DEVICE_DIR}
    )
    joypad_target_common(${TARGET})
endfunction()

# ============================================================================
# BOARD CONFIGURATION
# ============================================================================

if (PICO_BOARD STREQUAL "adafruit_kb2040")
    set(BOARD_BUTTON_GPIO 11)
elseif (PICO_BOARD STREQUAL "adafruit_qtpy_rp2040")
    set(BOARD_BUTTON_GPIO 21)
elseif (PICO_BOARD STREQUAL "adafruit_macropad_rp2040")
    set(BOARD_BUTTON_GPIO 0)
elseif (PICO_BOARD STREQUAL "adafruit_feather_rp2040")
    set(BOARD_BUTTON_GPIO 7)
else()
    set(BOARD_BUTTON_GPIO 7)
endif()
message(STATUS "Board ${PICO_BOARD}: BUTTON_USER_GPIO=${BOARD_BUTTON_GPIO}")

# ============================================================================
# USB HOST CONSOLE APPS (USB input -> retro console output)
# ============================================================================

# --- PCEngine ---
add_executable(joypad_pce)
target_compile_definitions(joypad_pce PRIVATE CONFIG_PCE=1)
if (PICO_BOARD STREQUAL "pico")
    target_compile_definitions(joypad_pce PRIVATE RPI_PICO_BUILD=1)
endif()
target_sources(joypad_pce PUBLIC ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/pcengine/pcengine_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2pce/app.c
)
target_include_directories(joypad_pce PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2pce
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/pcengine
)
target_link_libraries(joypad_pce PRIVATE ${COMMON_LIBRARIES})
joypad_target_common(joypad_pce)
joypad_add_btstack(joypad_pce)
pico_generate_pio_header(joypad_pce ${CMAKE_CURRENT_LIST_DIR}/native/device/pcengine/plex.pio)
pico_generate_pio_header(joypad_pce ${CMAKE_CURRENT_LIST_DIR}/native/device/pcengine/clock.pio)
pico_generate_pio_header(joypad_pce ${CMAKE_CURRENT_LIST_DIR}/native/device/pcengine/select.pio)

# --- GameCube (KB2040) ---
# CDC config mode: when no GC 3.3V detected, boots as USB device with CDC for web config
add_executable(joypad_ngc)
target_compile_definitions(joypad_ngc PRIVATE CONFIG_NGC=1 CONFIG_USB_HOST=1)
target_sources(joypad_ngc PUBLIC ${COMMON_SOURCES}
    ${USB_DEVICE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/gamecube/gamecube_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/src/joybus.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/src/GamecubeConsole.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2gc/app.c
)
target_include_directories(joypad_ngc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2gc
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/gamecube
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/include
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
)
target_link_libraries(joypad_ngc PRIVATE ${COMMON_LIBRARIES} hardware_flash tinyusb_device)
joypad_target_common(joypad_ngc)
joypad_add_btstack(joypad_ngc)
pico_generate_pio_header(joypad_ngc ${CMAKE_CURRENT_LIST_DIR}/lib/joybus-pio/src/joybus.pio)

# --- GameCube (RP2040-Zero) ---
# Note: Uses native USB host (not PIO USB) to avoid PIO conflict with joybus
# Override default I2C pins (6/7) to avoid conflict with GameCube pins (GC_DATA=7, GC_3V3=6)
# CDC config mode: when no GC 3.3V detected, boots as USB device with CDC for web config
add_executable(joypad_ngc_rp2040zero)
target_compile_definitions(joypad_ngc_rp2040zero PRIVATE
    CONFIG_NGC=1 CONFIG_USB_HOST=1
    WS2812_PIN=16
    USE_BOOTSEL_BUTTON=1
    PICO_DEFAULT_I2C_SDA_PIN=14
    PICO_DEFAULT_I2C_SCL_PIN=15
)
target_sources(joypad_ngc_rp2040zero PUBLIC ${COMMON_SOURCES}
    ${USB_DEVICE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/gamecube/gamecube_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/src/joybus.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/src/GamecubeConsole.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2gc/app.c
)
target_include_directories(joypad_ngc_rp2040zero PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2gc
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/gamecube
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/include
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
)
target_link_libraries(joypad_ngc_rp2040zero PRIVATE ${COMMON_LIBRARIES} hardware_flash tinyusb_device)
joypad_target_common(joypad_ngc_rp2040zero)
joypad_add_btstack(joypad_ngc_rp2040zero)
pico_generate_pio_header(joypad_ngc_rp2040zero ${CMAKE_CURRENT_LIST_DIR}/lib/joybus-pio/src/joybus.pio)

# --- Nuon Serial Adapter ---
# Polyface TYPE=0x04 serial device for Nuon homebrew debug logging.
# No USB host — uses USB device CDC to bridge SDATA register to PC terminal.
add_executable(joypad_nuonserial)
target_compile_definitions(joypad_nuonserial PRIVATE
    CONFIG_NUONSERIAL=1
    CONFIG_NO_NEOPIXEL=1
    CONFIG_NO_FLASH_LOCKOUT=1
    DISABLE_USB_HOST=1
)
target_sources(joypad_nuonserial PUBLIC ${CORE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/nuon/nuon_serial_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/nuonserial/app.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/nuonserial/usb_descriptors.c
)
target_include_directories(joypad_nuonserial PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/nuonserial
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/nuon
)
target_link_libraries(joypad_nuonserial PRIVATE
    pico_stdlib pico_multicore hardware_pio pico_rand pico_bit_ops
    tinyusb_device tinyusb_board pico_unique_id
)
joypad_target_common(joypad_nuonserial)
pico_generate_pio_header(joypad_nuonserial ${CMAKE_CURRENT_LIST_DIR}/native/device/nuon/polyface_read.pio)
pico_generate_pio_header(joypad_nuonserial ${CMAKE_CURRENT_LIST_DIR}/native/device/nuon/polyface_send.pio)

# --- Nuon ---
add_executable(joypad_nuon)
target_compile_definitions(joypad_nuon PRIVATE CONFIG_NUON=1 CONFIG_NO_NEOPIXEL=1)
target_sources(joypad_nuon PUBLIC ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/nuon/nuon_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2nuon/app.c
)
target_include_directories(joypad_nuon PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2nuon
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/nuon
)
target_link_libraries(joypad_nuon PRIVATE ${COMMON_LIBRARIES} pico_bit_ops)
joypad_target_common(joypad_nuon)
joypad_add_btstack(joypad_nuon)
pico_generate_pio_header(joypad_nuon ${CMAKE_CURRENT_LIST_DIR}/native/device/nuon/polyface_read.pio)
pico_generate_pio_header(joypad_nuon ${CMAKE_CURRENT_LIST_DIR}/native/device/nuon/polyface_send.pio)

# --- N64 ---
add_executable(joypad_n64)
target_compile_definitions(joypad_n64 PRIVATE CONFIG_N64=1)
target_sources(joypad_n64 PUBLIC ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/n64/n64_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/src/joybus.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/src/N64Console.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2n64/app.c
)
target_include_directories(joypad_n64 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2n64
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/n64
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/include
)
target_link_libraries(joypad_n64 PRIVATE ${COMMON_LIBRARIES} hardware_flash)
joypad_target_common(joypad_n64)
joypad_add_btstack(joypad_n64)
pico_generate_pio_header(joypad_n64 ${CMAKE_CURRENT_LIST_DIR}/lib/joybus-pio/src/joybus.pio)

# --- Loopy ---
add_executable(joypad_loopy)
target_compile_definitions(joypad_loopy PRIVATE CONFIG_LOOPY=1)
target_sources(joypad_loopy PUBLIC ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/loopy/loopy_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2loopy/app.c
)
target_include_directories(joypad_loopy PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2loopy
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/loopy
)
target_link_libraries(joypad_loopy PRIVATE ${COMMON_LIBRARIES} pico_bit_ops)
joypad_target_common(joypad_loopy)
joypad_add_btstack(joypad_loopy)
pico_generate_pio_header(joypad_loopy ${CMAKE_CURRENT_LIST_DIR}/native/device/loopy/loopy.pio)

# --- Dreamcast ---
add_executable(joypad_dc)
target_compile_definitions(joypad_dc PRIVATE CONFIG_DC=1 CONFIG_USB_HOST=1 CONFIG_DC_CORE1_TX=1)
target_sources(joypad_dc PUBLIC ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/dreamcast/dreamcast_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/dreamcast/maple_state_machine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2dc/app.c
)
target_include_directories(joypad_dc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2dc
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/dreamcast
)
target_link_libraries(joypad_dc PRIVATE ${COMMON_LIBRARIES} hardware_dma)
joypad_target_common(joypad_dc)
joypad_add_btstack(joypad_dc)
pico_generate_pio_header(joypad_dc ${CMAKE_CURRENT_LIST_DIR}/native/device/dreamcast/maple.pio)

# --- Dreamcast (RP2040-Zero) ---
# USB4Maple-compatible pinout: GPIO 14/15 for Maple Bus
add_executable(joypad_dc_rp2040zero)
target_compile_definitions(joypad_dc_rp2040zero PRIVATE
    CONFIG_DC=1 CONFIG_USB_HOST=1 CONFIG_DC_CORE1_TX=1
    MAPLE_PIN1=14
    MAPLE_PIN5=15
    WS2812_PIN=16
    USE_BOOTSEL_BUTTON=1
)
target_sources(joypad_dc_rp2040zero PUBLIC ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/dreamcast/dreamcast_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/dreamcast/maple_state_machine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2dc/app.c
)
target_include_directories(joypad_dc_rp2040zero PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2dc
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/dreamcast
)
target_link_libraries(joypad_dc_rp2040zero PRIVATE ${COMMON_LIBRARIES} hardware_dma)
joypad_target_common(joypad_dc_rp2040zero)
joypad_add_btstack(joypad_dc_rp2040zero)
pico_generate_pio_header(joypad_dc_rp2040zero ${CMAKE_CURRENT_LIST_DIR}/native/device/dreamcast/maple.pio)

# --- NEOGEO ---
add_executable(joypad_neogeo)
target_compile_definitions(joypad_neogeo PRIVATE CONFIG_NEOGEO=1)
target_sources(joypad_neogeo PUBLIC ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/gpio/gpio_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2neogeo/app.c
)
target_include_directories(joypad_neogeo PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2neogeo
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/gpio
)
target_link_libraries(joypad_neogeo PRIVATE ${COMMON_LIBRARIES})
joypad_target_common(joypad_neogeo)
joypad_add_btstack(joypad_neogeo)

# Neo Geo — Pi Pico (WS2812 PIO on GP25 drives onboard LED as on/off indicator)
add_executable(joypad_neogeo_pico)
target_compile_definitions(joypad_neogeo_pico PRIVATE
    CONFIG_NEOGEO=1
    WS2812_PIN=25
    USE_BOOTSEL_BUTTON=1
    RPI_PICO_BUILD=1
)
target_sources(joypad_neogeo_pico PUBLIC ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/gpio/gpio_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2neogeo/app.c
)
target_include_directories(joypad_neogeo_pico PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2neogeo
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/neogeo
)
target_link_libraries(joypad_neogeo_pico PRIVATE ${COMMON_LIBRARIES})
joypad_target_common(joypad_neogeo_pico)
joypad_add_btstack(joypad_neogeo_pico)

# Neo Geo — RP2040-Zero (WS2812 on GP16, auto from board def)
add_executable(joypad_neogeo_rp2040zero)
target_compile_definitions(joypad_neogeo_rp2040zero PRIVATE
    CONFIG_NEOGEO=1
    PICO_RP2040_ZERO_BUILD=1
)
target_sources(joypad_neogeo_rp2040zero PUBLIC ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/gpio/gpio_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2neogeo/app.c
)
target_include_directories(joypad_neogeo_rp2040zero PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2neogeo
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/neogeo
)
target_link_libraries(joypad_neogeo_rp2040zero PRIVATE ${COMMON_LIBRARIES})
joypad_target_common(joypad_neogeo_rp2040zero)
joypad_add_btstack(joypad_neogeo_rp2040zero)

# --- N64 to Dreamcast ---
add_executable(joypad_n642dc)
# Core 0 TX mode required: joybus uses PIO1 SM3, conflicts with Core 1 TX
# Core 0: N64 joybus polling + maple TX, Core 1: Maple RX only
target_compile_definitions(joypad_n642dc PRIVATE CONFIG_N642DC=1 CONFIG_DC=1 CONFIG_NO_NEOPIXEL=1 N64_PIN_DATA=29)
target_sources(joypad_n642dc PUBLIC ${COMMON_SOURCES}
    ${N64_HOST_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/dreamcast/dreamcast_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/dreamcast/maple_state_machine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/n642dc/app.c
)
target_include_directories(joypad_n642dc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/n642dc
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/n64
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/dreamcast
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/include
)
target_link_libraries(joypad_n642dc PRIVATE ${COMMON_LIBRARIES} hardware_dma)
joypad_target_common(joypad_n642dc)
# No BTstack for n642dc - uses native N64 input, not USB host
pico_generate_pio_header(joypad_n642dc ${CMAKE_CURRENT_LIST_DIR}/lib/joybus-pio/src/joybus.pio)
pico_generate_pio_header(joypad_n642dc ${CMAKE_CURRENT_LIST_DIR}/native/device/dreamcast/maple.pio)

# --- N64 to Dreamcast (Pico 2 W) ---
# Pinout: DC SDCKA=GPIO14, SDCKB=GPIO15 (physical pins 19/20)
#         N64 Data=GPIO18 (physical pin 24)
#         Power: DC 5V=VSYS(pin39), N64 3.3V=3V3OUT(pin36), GND=pins 23/38
add_executable(joypad_n642dc_pico2_w)
target_compile_definitions(joypad_n642dc_pico2_w PRIVATE
    CONFIG_N642DC=1
    CONFIG_DC=1
    CONFIG_NO_NEOPIXEL=1
    USE_BOOTSEL_BUTTON=1
    MAPLE_PIN1=14
    MAPLE_PIN5=15
    N64_PIN_DATA=18
)
target_sources(joypad_n642dc_pico2_w PUBLIC ${COMMON_SOURCES}
    ${N64_HOST_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/dreamcast/dreamcast_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/dreamcast/maple_state_machine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/n642dc/app.c
)
target_include_directories(joypad_n642dc_pico2_w PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/n642dc
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/n64
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/dreamcast
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/include
)
target_link_libraries(joypad_n642dc_pico2_w PRIVATE ${COMMON_LIBRARIES} hardware_dma)
joypad_target_common(joypad_n642dc_pico2_w)
# No BTstack for n642dc - uses native N64 input, not USB host
pico_generate_pio_header(joypad_n642dc_pico2_w ${CMAKE_CURRENT_LIST_DIR}/lib/joybus-pio/src/joybus.pio)
pico_generate_pio_header(joypad_n642dc_pico2_w ${CMAKE_CURRENT_LIST_DIR}/native/device/dreamcast/maple.pio)

# --- N64 to Nuon ---
add_executable(joypad_n642nuon)
target_compile_definitions(joypad_n642nuon PRIVATE CONFIG_N642NUON=1 CONFIG_NUON=1 CONFIG_NO_NEOPIXEL=1 N64_PIN_DATA=29)
target_sources(joypad_n642nuon PUBLIC ${COMMON_SOURCES}
    ${N64_HOST_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/nuon/nuon_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/n642nuon/app.c
)
target_include_directories(joypad_n642nuon PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/n642nuon
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/n64
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/nuon
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/include
)
target_link_libraries(joypad_n642nuon PRIVATE ${COMMON_LIBRARIES} pico_bit_ops)
joypad_target_common(joypad_n642nuon)
# No BTstack - native N64 input only
pico_generate_pio_header(joypad_n642nuon ${CMAKE_CURRENT_LIST_DIR}/lib/joybus-pio/src/joybus.pio)

# --- N64 to Nuon (Aries64 board) ---
# Aries64: RP2040 board with N64 data on GPIO11, Nuon polyface on GPIO12/13.
# GPIO10 is an open-drain driver transistor on the N64 data line (unused by firmware).
add_executable(joypad_n642nuon_aries64)
target_compile_definitions(joypad_n642nuon_aries64 PRIVATE
    CONFIG_N642NUON=1
    CONFIG_NUON=1
    CONFIG_NO_NEOPIXEL=1
    N64_PIN_DATA=11
    DATAIO_PIN=12
    BOARD_LED_PIN=25
)
target_sources(joypad_n642nuon_aries64 PUBLIC ${COMMON_SOURCES}
    ${N64_HOST_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/nuon/nuon_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/n642nuon/app.c
)
target_include_directories(joypad_n642nuon_aries64 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/n642nuon
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/n64
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/nuon
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/include
)
target_link_libraries(joypad_n642nuon_aries64 PRIVATE ${COMMON_LIBRARIES} pico_bit_ops)
joypad_target_common(joypad_n642nuon_aries64)
pico_generate_pio_header(joypad_n642nuon_aries64 ${CMAKE_CURRENT_LIST_DIR}/lib/joybus-pio/src/joybus.pio)
pico_generate_pio_header(joypad_n642nuon_aries64 ${CMAKE_CURRENT_LIST_DIR}/native/device/nuon/polyface_read.pio)
pico_generate_pio_header(joypad_n642nuon_aries64 ${CMAKE_CURRENT_LIST_DIR}/native/device/nuon/polyface_send.pio)
pico_generate_pio_header(joypad_n642nuon ${CMAKE_CURRENT_LIST_DIR}/native/device/nuon/polyface_read.pio)
pico_generate_pio_header(joypad_n642nuon ${CMAKE_CURRENT_LIST_DIR}/native/device/nuon/polyface_send.pio)

# --- 3DO ---
add_executable(joypad_3do)
target_compile_definitions(joypad_3do PRIVATE CONFIG_3DO=1)
target_sources(joypad_3do PUBLIC ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/3do/3do_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb23do/app.c
)
target_include_directories(joypad_3do PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb23do
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/3do
)
target_link_libraries(joypad_3do PRIVATE ${COMMON_LIBRARIES} hardware_dma)
joypad_target_common(joypad_3do)
joypad_add_btstack(joypad_3do)
pico_generate_pio_header(joypad_3do ${CMAKE_CURRENT_LIST_DIR}/native/device/3do/sampling.pio)
pico_generate_pio_header(joypad_3do ${CMAKE_CURRENT_LIST_DIR}/native/device/3do/output.pio)

# --- SNES to 3DO ---
add_executable(joypad_snes3do)
target_compile_definitions(joypad_snes3do PRIVATE CONFIG_SNES3DO=1)
target_sources(joypad_snes3do PUBLIC ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/3do/3do_device.c
    ${SNES_HOST_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/snes23do/app.c
)
target_include_directories(joypad_snes3do PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/snes23do
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/3do
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/snes
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/SNESpad/src
)
target_link_libraries(joypad_snes3do PRIVATE ${COMMON_LIBRARIES} hardware_dma)
joypad_target_common(joypad_snes3do)
# No BTstack for snes3do - uses SNES input, not USB host
pico_generate_pio_header(joypad_snes3do ${CMAKE_CURRENT_LIST_DIR}/native/device/3do/sampling.pio)
pico_generate_pio_header(joypad_snes3do ${CMAKE_CURRENT_LIST_DIR}/native/device/3do/output.pio)

# --- UART Bridge ---
add_executable(joypad_uart)
target_compile_definitions(joypad_uart PRIVATE CONFIG_UART=1)
target_sources(joypad_uart PUBLIC ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/uart/uart_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2uart/app.c
)
target_include_directories(joypad_uart PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2uart
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/uart
)
target_link_libraries(joypad_uart PRIVATE ${COMMON_LIBRARIES} hardware_uart)
joypad_target_common(joypad_uart)
joypad_add_btstack(joypad_uart)

# ============================================================================
# USB HOST TO USB DEVICE APPS (USB input -> USB output)
# ============================================================================
# USB Host Methods:
#   PIO-USB (tinyusb_pico_pio_usb) - Software USB host via PIO, uses 2 GPIOs
#   MAX3421E (tinyusb_max3421)     - SPI USB host IC, uses SPI bus + CS + INT pins
#   Native (tinyusb_host)          - RP2040's built-in USB, only when no device output needed

# --- USB to USB (Feather RP2040 #4884, PIO-USB) ---
add_executable(joypad_usb_feather_rp2040)
target_compile_definitions(joypad_usb_feather_rp2040 PRIVATE
    CONFIG_USB=1
    PICO_DEFAULT_PIO_USB_DP_PIN=16
    BOARD_TUH_RHPORT=1
    USBR_CDC_DEBUG=1
    PICO_DEFAULT_UART=0
    PICO_DEFAULT_UART_TX_PIN=0
    PICO_DEFAULT_UART_RX_PIN=1
)
target_sources(joypad_usb_feather_rp2040 PUBLIC
    ${COMMON_SOURCES}
    ${USB_DEVICE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb/app.c
)
target_include_directories(joypad_usb_feather_rp2040 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
)
target_link_libraries(joypad_usb_feather_rp2040 PRIVATE ${COMMON_LIBRARIES} tinyusb_device tinyusb_pico_pio_usb)
joypad_target_common(joypad_usb_feather_rp2040)
joypad_add_btstack(joypad_usb_feather_rp2040)
pico_enable_stdio_uart(joypad_usb_feather_rp2040 1)

# --- USB to USB (Feather RP2040 USB Host #5723, PIO-USB) ---
add_executable(joypad_usb_feather_rp2040_usb_host)
target_compile_definitions(joypad_usb_feather_rp2040_usb_host PRIVATE
    CONFIG_USB=1
    PICO_DEFAULT_PIO_USB_DP_PIN=16
    PICO_DEFAULT_PIO_USB_VBUSEN_PIN=18
    PICO_DEFAULT_PIO_USB_VBUSEN_STATE=1
    BOARD_TUH_RHPORT=1
    USBR_CDC_DEBUG=1
    PICO_DEFAULT_UART=0
    PICO_DEFAULT_UART_TX_PIN=0
    PICO_DEFAULT_UART_RX_PIN=1
)
target_sources(joypad_usb_feather_rp2040_usb_host PUBLIC
    ${COMMON_SOURCES}
    ${USB_DEVICE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb/app.c
)
target_include_directories(joypad_usb_feather_rp2040_usb_host PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
)
target_link_libraries(joypad_usb_feather_rp2040_usb_host PRIVATE ${COMMON_LIBRARIES} tinyusb_device tinyusb_pico_pio_usb)
joypad_target_common(joypad_usb_feather_rp2040_usb_host)
joypad_add_btstack(joypad_usb_feather_rp2040_usb_host)
pico_enable_stdio_uart(joypad_usb_feather_rp2040_usb_host 1)

# --- USB to USB (Feather RP2040 #4884 + MAX3421E USB Host FeatherWing #5858) ---
# MAX3421E pin defines for TinyUSB family.c BSP + max3421_host.c init
# FeatherWing SPI on Feather header: SPI0 (SCK=18, MOSI=19, MISO=20), CS=10, INT=9
add_executable(joypad_usb_feather_rp2040_max3421)
target_compile_definitions(joypad_usb_feather_rp2040_max3421 PRIVATE
    CONFIG_USB=1
    BOARD_TUH_RHPORT=1
    USBR_CDC_DEBUG=1
    PICO_DEFAULT_UART=0
    PICO_DEFAULT_UART_TX_PIN=0
    PICO_DEFAULT_UART_RX_PIN=1
    MAX3421_SPI=spi0
    MAX3421_SCK_PIN=18
    MAX3421_MOSI_PIN=19
    MAX3421_MISO_PIN=20
    MAX3421_CS_PIN=10
    MAX3421_INTR_PIN=9
    USE_BOOTSEL_BUTTON=1
    # FeatherWing OLED 128x64 (SH1107) over I2C on STEMMA QT
    OLED_I2C_INST=1
    OLED_I2C_SDA_PIN=2
    OLED_I2C_SCL_PIN=3
    OLED_I2C_ADDR=0x3C
    OLED_BUTTON_B_PIN=6
    OLED_BUTTON_C_PIN=5
    I2C_PEER_ENABLED=1
)
target_sources(joypad_usb_feather_rp2040_max3421 PUBLIC
    ${COMMON_SOURCES}
    ${USB_DEVICE_SOURCES}
    ${I2C_PEER_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/display/display.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/display/display_i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/display/joy_anim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb/app.c
)
target_include_directories(joypad_usb_feather_rp2040_max3421 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
)
target_link_libraries(joypad_usb_feather_rp2040_max3421 PRIVATE ${COMMON_LIBRARIES} tinyusb_device tinyusb_max3421 hardware_i2c pico_i2c_slave)
joypad_target_common(joypad_usb_feather_rp2040_max3421)
joypad_add_btstack(joypad_usb_feather_rp2040_max3421)
pico_enable_stdio_uart(joypad_usb_feather_rp2040_max3421 1)

# --- USB to USB (Feather RP2040 USB Host #5723 + MAX3421E USB Host FeatherWing #5858) ---
# Uses MAX3421E FeatherWing instead of onboard PIO USB host.
# Board SPI1: SCK=14, MOSI=15, MISO=8. NeoPixel on GPIO 21 (power gate GPIO 20).
add_executable(joypad_usb_feather_rp2040_usb_host_max3421)
target_compile_definitions(joypad_usb_feather_rp2040_usb_host_max3421 PRIVATE
    CONFIG_USB=1
    BOARD_TUH_RHPORT=1
    USBR_CDC_DEBUG=1
    PICO_DEFAULT_UART=0
    PICO_DEFAULT_UART_TX_PIN=0
    PICO_DEFAULT_UART_RX_PIN=1
    MAX3421_SPI=spi1
    MAX3421_SCK_PIN=14
    MAX3421_MOSI_PIN=15
    MAX3421_MISO_PIN=8
    MAX3421_CS_PIN=10
    MAX3421_INTR_PIN=9
    # FeatherWing OLED 128x64 (SH1107) over I2C on STEMMA QT
    OLED_I2C_INST=1
    OLED_I2C_SDA_PIN=2
    OLED_I2C_SCL_PIN=3
    OLED_I2C_ADDR=0x3C
    OLED_BUTTON_B_PIN=6
    OLED_BUTTON_C_PIN=5
    I2C_PEER_ENABLED=1
)
target_sources(joypad_usb_feather_rp2040_usb_host_max3421 PUBLIC
    ${COMMON_SOURCES}
    ${USB_DEVICE_SOURCES}
    ${I2C_PEER_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/display/display.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/display/display_i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/display/joy_anim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb/app.c
)
target_include_directories(joypad_usb_feather_rp2040_usb_host_max3421 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
)
target_link_libraries(joypad_usb_feather_rp2040_usb_host_max3421 PRIVATE ${COMMON_LIBRARIES} tinyusb_device tinyusb_max3421 hardware_i2c pico_i2c_slave)
# Wrap buggy tuh_max3421_spi_xfer_api in TinyUSB BSP (hardcodes spi0 instead of MAX3421_SPI)
target_link_options(joypad_usb_feather_rp2040_usb_host_max3421 PRIVATE -Wl,--wrap=tuh_max3421_spi_xfer_api)
joypad_target_common(joypad_usb_feather_rp2040_usb_host_max3421)
joypad_add_btstack(joypad_usb_feather_rp2040_usb_host_max3421)
pico_enable_stdio_uart(joypad_usb_feather_rp2040_usb_host_max3421 1)

# --- USB to USB (Raspberry Pi Pico, PIO-USB) ---
# PIO USB host on GP16/GP17 (default), standard LED on GP25
add_executable(joypad_usb_pico)
target_compile_definitions(joypad_usb_pico PRIVATE
    CONFIG_USB=1
    PICO_DEFAULT_PIO_USB_DP_PIN=16
    BOARD_TUH_RHPORT=1
    USE_BOOTSEL_BUTTON=1
)
target_sources(joypad_usb_pico PUBLIC ${COMMON_SOURCES} ${USB_DEVICE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb/app.c
)
target_include_directories(joypad_usb_pico PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
)
target_link_libraries(joypad_usb_pico PRIVATE ${COMMON_LIBRARIES} tinyusb_device tinyusb_pico_pio_usb)
joypad_target_common(joypad_usb_pico)
joypad_add_btstack(joypad_usb_pico)

# --- USB to USB (Raspberry Pi Pico W, PIO-USB) ---
# PIO USB host on GP16/GP17, no WS2812, LED behind CYW43
add_executable(joypad_usb_pico_w)
target_compile_definitions(joypad_usb_pico_w PRIVATE
    CONFIG_USB=1
    PICO_DEFAULT_PIO_USB_DP_PIN=16
    BOARD_TUH_RHPORT=1
    USE_BOOTSEL_BUTTON=1
)
target_sources(joypad_usb_pico_w PUBLIC ${COMMON_SOURCES} ${USB_DEVICE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb/app.c
)
target_include_directories(joypad_usb_pico_w PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
)
target_link_libraries(joypad_usb_pico_w PRIVATE ${COMMON_LIBRARIES} tinyusb_device tinyusb_pico_pio_usb)
joypad_target_common(joypad_usb_pico_w)
joypad_add_btstack(joypad_usb_pico_w)

# --- USB to USB (Raspberry Pi Pico 2 W, PIO-USB) ---
# PIO USB host on GP16/GP17, no WS2812, LED behind CYW43
add_executable(joypad_usb_pico2_w)
target_compile_definitions(joypad_usb_pico2_w PRIVATE
    CONFIG_USB=1
    PICO_DEFAULT_PIO_USB_DP_PIN=16
    BOARD_TUH_RHPORT=1
    USE_BOOTSEL_BUTTON=1
)
target_sources(joypad_usb_pico2_w PUBLIC ${COMMON_SOURCES} ${USB_DEVICE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb/app.c
)
target_include_directories(joypad_usb_pico2_w PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
)
target_link_libraries(joypad_usb_pico2_w PRIVATE ${COMMON_LIBRARIES} tinyusb_device tinyusb_pico_pio_usb)
joypad_target_common(joypad_usb_pico2_w)
joypad_add_btstack(joypad_usb_pico2_w)

# --- USB to USB (RP2040-Zero, PIO-USB) ---
add_executable(joypad_usb_rp2040zero)
target_compile_definitions(joypad_usb_rp2040zero PRIVATE
    CONFIG_USB=1
    CONFIG_PIO_USB_DP_PIN=10
    PICO_DEFAULT_PIO_USB_DP_PIN=10
    BOARD_TUH_RHPORT=1
    WS2812_PIN=16
    USE_BOOTSEL_BUTTON=1
)
target_sources(joypad_usb_rp2040zero PUBLIC ${COMMON_SOURCES} ${USB_DEVICE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb/app.c
)
target_include_directories(joypad_usb_rp2040zero PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
)
target_link_libraries(joypad_usb_rp2040zero PRIVATE ${COMMON_LIBRARIES} tinyusb_device tinyusb_pico_pio_usb)
joypad_target_common(joypad_usb_rp2040zero)
joypad_add_btstack(joypad_usb_rp2040zero)

# --- USB to USB (Waveshare RP2350A, PIO-USB) ---
add_executable(joypad_usb_rp2350usba)
target_compile_definitions(joypad_usb_rp2350usba PRIVATE
    CONFIG_USB=1
    CONFIG_USB_HOST=1
    CONFIG_PIO_USB_DP_PIN=12
    PICO_DEFAULT_PIO_USB_DP_PIN=12
    BOARD_TUH_RHPORT=1
    WS2812_PIN=16
    USE_BOOTSEL_BUTTON=1
    APP_NAME="usb2usb"
)
target_sources(joypad_usb_rp2350usba PUBLIC ${COMMON_SOURCES} ${USB_DEVICE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb/app.c
)
target_include_directories(joypad_usb_rp2350usba PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
)
target_link_libraries(joypad_usb_rp2350usba PRIVATE ${COMMON_LIBRARIES} tinyusb_device tinyusb_pico_pio_usb)
joypad_target_common(joypad_usb_rp2350usba)
joypad_add_btstack(joypad_usb_rp2350usba)

# --- BT to USB (Pico W / Pico 2 W built-in Bluetooth) ---
# Only build when targeting Pico W or Pico 2 W
if (PICO_BOARD STREQUAL "pico_w" OR PICO_BOARD STREQUAL "pico2_w")
    add_executable(joypad_bt2usb)
    target_compile_definitions(joypad_bt2usb PRIVATE
        CONFIG_BT2USB=1
        CONFIG_USB=1
        DISABLE_USB_HOST=1
        USE_BOOTSEL_BUTTON=1
        CYW43_LWIP=0
        # BTstack uses async_context, not embedded run loop
        BTSTACK_USE_CYW43=1
        ENABLE_BTSTACK=1
    )
    target_sources(joypad_bt2usb PUBLIC
        ${CORE_SOURCES}
        ${USB_DEVICE_SOURCES}
        ${BTHID_DEVICE_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/generic/hid_parser.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack/btstack_host.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack/bt_device_db.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/transport/bt_transport.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/transport/bt_transport_cyw43.c
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/bt2usb/app.c
    )
    target_include_directories(joypad_bt2usb PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/bt2usb
        ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
        ${CMAKE_CURRENT_SOURCE_DIR}/bt
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid
        ${BTSTACK_INCLUDES}
    )
    target_link_libraries(joypad_bt2usb PRIVATE
        pico_stdlib
        pico_multicore
        pico_rand
        pico_cyw43_arch_poll
        pico_btstack_ble
        pico_btstack_classic
        pico_btstack_cyw43
        tinyusb_device
        tinyusb_board
    )
    joypad_target_common(joypad_bt2usb)
    pico_enable_stdio_usb(joypad_bt2usb 0)
    pico_enable_stdio_uart(joypad_bt2usb 1)

    # --- BT to Nuon (Pico W / Pico 2 W built-in Bluetooth -> Nuon) ---
    add_executable(joypad_bt2nuon)
    target_compile_definitions(joypad_bt2nuon PRIVATE
        CONFIG_BT2NUON=1
        DISABLE_USB_HOST=1
        DISABLE_USB_DEVICE=1
        CONFIG_NO_NEOPIXEL=1
        CONFIG_NO_FLASH_LOCKOUT=1
        USE_BOOTSEL_BUTTON=1
        CYW43_LWIP=0
        BTSTACK_USE_CYW43=1
        ENABLE_BTSTACK=1
        SCAN_INTERVAL=0x0500
        SCAN_WINDOW=0x0030
    )
    target_sources(joypad_bt2nuon PUBLIC
        ${CORE_SOURCES}
        ${USB_DEVICE_SOURCES}
        ${BTHID_DEVICE_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/generic/hid_parser.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack/btstack_host.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack/bt_device_db.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/transport/bt_transport.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/transport/bt_transport_cyw43.c
        ${CMAKE_CURRENT_SOURCE_DIR}/native/device/nuon/nuon_device.c
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/bt2nuon/app.c
    )
    target_include_directories(joypad_bt2nuon PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/bt2nuon
        ${CMAKE_CURRENT_SOURCE_DIR}/native/device/nuon
        ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
        ${CMAKE_CURRENT_SOURCE_DIR}/bt
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid
        ${BTSTACK_INCLUDES}
    )
    # Link tinyusb for headers but disable USB device initialization.
    # USB device DMA/interrupts interfere with Core 1's polyface timing.
    target_link_libraries(joypad_bt2nuon PRIVATE
        pico_stdlib
        pico_multicore
        pico_rand
        pico_bit_ops
        hardware_pio
        hardware_flash
        pico_cyw43_arch_poll
        pico_btstack_ble
        pico_btstack_classic
        pico_btstack_cyw43
        tinyusb_device
        tinyusb_board
    )
    joypad_target_common(joypad_bt2nuon)
    pico_enable_stdio_usb(joypad_bt2nuon 0)
    pico_enable_stdio_uart(joypad_bt2nuon 1)
    pico_generate_pio_header(joypad_bt2nuon ${CMAKE_CURRENT_LIST_DIR}/native/device/nuon/polyface_read.pio)
    pico_generate_pio_header(joypad_bt2nuon ${CMAKE_CURRENT_LIST_DIR}/native/device/nuon/polyface_send.pio)

    # --- BT to Loopy (Pico W / Pico 2 W built-in Bluetooth -> Casio Loopy) ---
    add_executable(joypad_bt2loopy)
    target_compile_definitions(joypad_bt2loopy PRIVATE
        CONFIG_BT2LOOPY=1
        DISABLE_USB_HOST=1
        USE_BOOTSEL_BUTTON=1
        CYW43_LWIP=0
        BTSTACK_USE_CYW43=1
        ENABLE_BTSTACK=1
    )
    target_sources(joypad_bt2loopy PUBLIC
        ${CORE_SOURCES}
        ${USB_DEVICE_SOURCES}
        ${BTHID_DEVICE_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/generic/hid_parser.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack/btstack_host.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack/bt_device_db.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/transport/bt_transport.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/transport/bt_transport_cyw43.c
        ${CMAKE_CURRENT_SOURCE_DIR}/native/device/loopy/loopy_device.c
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/bt2loopy/app.c
    )
    target_include_directories(joypad_bt2loopy PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/bt2loopy
        ${CMAKE_CURRENT_SOURCE_DIR}/native/device/loopy
        ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
        ${CMAKE_CURRENT_SOURCE_DIR}/bt
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid
        ${BTSTACK_INCLUDES}
    )
    target_link_libraries(joypad_bt2loopy PRIVATE
        pico_stdlib
        pico_multicore
        pico_rand
        pico_bit_ops
        hardware_pio
        pico_cyw43_arch_poll
        pico_btstack_ble
        pico_btstack_classic
        pico_btstack_cyw43
        tinyusb_device
        tinyusb_board
    )
    joypad_target_common(joypad_bt2loopy)
    pico_enable_stdio_usb(joypad_bt2loopy 0)
    pico_enable_stdio_uart(joypad_bt2loopy 1)
    pico_generate_pio_header(joypad_bt2loopy ${CMAKE_CURRENT_LIST_DIR}/native/device/loopy/loopy.pio)

    # --- BT to N64 (Pico W / Pico 2 W built-in Bluetooth -> N64 Console) ---
    add_executable(joypad_bt2n64)
    target_compile_definitions(joypad_bt2n64 PRIVATE
        CONFIG_BT2N64=1
        DISABLE_USB_HOST=1
        CONFIG_NO_NEOPIXEL=1
        USE_BOOTSEL_BUTTON=1
        CYW43_LWIP=0
        BTSTACK_USE_CYW43=1
        ENABLE_BTSTACK=1
        N64_DATA_PIN=2
    )
    target_sources(joypad_bt2n64 PUBLIC
        ${CORE_SOURCES}
        ${BTHID_DEVICE_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/generic/hid_parser.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack/btstack_host.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack/bt_device_db.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/transport/bt_transport.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/transport/bt_transport_cyw43.c
        ${CMAKE_CURRENT_SOURCE_DIR}/native/device/n64/n64_device.c
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/src/joybus.c
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/src/N64Console.c
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/bt2n64/app.c
    )
    target_include_directories(joypad_bt2n64 PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/bt2n64
        ${CMAKE_CURRENT_SOURCE_DIR}/native/device/n64
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/include
        ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
        ${CMAKE_CURRENT_SOURCE_DIR}/bt
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid
        ${BTSTACK_INCLUDES}
    )
    target_link_libraries(joypad_bt2n64 PRIVATE
        pico_stdlib
        pico_multicore
        pico_rand
        hardware_pio
        hardware_flash
        pico_cyw43_arch_poll
        pico_btstack_ble
        pico_btstack_classic
        pico_btstack_cyw43
    )
    joypad_target_common(joypad_bt2n64)
    pico_enable_stdio_usb(joypad_bt2n64 1)
    pico_enable_stdio_uart(joypad_bt2n64 1)
    pico_generate_pio_header(joypad_bt2n64 ${CMAKE_CURRENT_LIST_DIR}/lib/joybus-pio/src/joybus.pio)

    # --- BT to GameCube (Pico W / Pico 2 W built-in Bluetooth -> GameCube) ---
    add_executable(joypad_bt2gc)
    target_compile_definitions(joypad_bt2gc PRIVATE
        CONFIG_BT2GC=1
        DISABLE_USB_HOST=1
        CONFIG_NO_NEOPIXEL=1
        USE_BOOTSEL_BUTTON=1
        CYW43_LWIP=0
        BTSTACK_USE_CYW43=1
        ENABLE_BTSTACK=1
    )
    target_sources(joypad_bt2gc PUBLIC
        ${CORE_SOURCES}
        ${USB_DEVICE_SOURCES}
        ${BTHID_DEVICE_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/generic/hid_parser.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack/btstack_host.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack/bt_device_db.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/transport/bt_transport.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/transport/bt_transport_cyw43.c
        ${CMAKE_CURRENT_SOURCE_DIR}/native/device/gamecube/gamecube_device.c
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/src/joybus.c
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/src/GamecubeConsole.c
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/bt2gc/app.c
    )
    target_include_directories(joypad_bt2gc PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/bt2gc
        ${CMAKE_CURRENT_SOURCE_DIR}/native/device/gamecube
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/include
        ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
        ${CMAKE_CURRENT_SOURCE_DIR}/bt
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid
        ${BTSTACK_INCLUDES}
    )
    target_link_libraries(joypad_bt2gc PRIVATE
        pico_stdlib
        pico_multicore
        pico_rand
        hardware_pio
        hardware_flash
        pico_cyw43_arch_poll
        pico_btstack_ble
        pico_btstack_classic
        pico_btstack_cyw43
        tinyusb_device
        tinyusb_board
    )
    joypad_target_common(joypad_bt2gc)
    pico_enable_stdio_usb(joypad_bt2gc 0)
    pico_enable_stdio_uart(joypad_bt2gc 1)
    pico_generate_pio_header(joypad_bt2gc ${CMAKE_CURRENT_LIST_DIR}/lib/joybus-pio/src/joybus.pio)

    # --- BT+USB to USB (Pico W / Pico 2 W: PIO-USB host + CYW43 Bluetooth + USB device) ---
    # Combines USB host (PIO-USB on GP16/17) with built-in CYW43 Bluetooth.
    # No pin conflicts: PIO-USB uses GP16/17, CYW43 uses dedicated SPI pins (23/24/25/29).
    # Does NOT use joypad_add_btstack() — CYW43 uses pico_btstack_* libs instead of
    # compiled BTstack sources to avoid duplicate symbol conflicts.
    add_executable(joypad_btusb2usb)
    target_compile_definitions(joypad_btusb2usb PRIVATE
        CONFIG_BTUSB2USB=1
        CONFIG_USB=1
        CONFIG_NO_NEOPIXEL=1
        PICO_DEFAULT_PIO_USB_DP_PIN=16
        BOARD_TUH_RHPORT=1
        USE_BOOTSEL_BUTTON=1
        CYW43_LWIP=0
        BTSTACK_USE_CYW43=1
        ENABLE_BTSTACK=1
    )
    target_sources(joypad_btusb2usb PUBLIC
        ${COMMON_SOURCES}
        ${USB_DEVICE_SOURCES}
        ${BTHID_DEVICE_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/generic/hid_parser.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack/btstack_host.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack/bt_device_db.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/transport/bt_transport.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/transport/bt_transport_cyw43.c
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb/app.c
    )
    target_include_directories(joypad_btusb2usb PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb
        ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
        ${CMAKE_CURRENT_SOURCE_DIR}/bt
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid
        ${BTSTACK_INCLUDES}
    )
    target_link_libraries(joypad_btusb2usb PRIVATE
        ${COMMON_LIBRARIES}
        tinyusb_device
        tinyusb_pico_pio_usb
        pico_cyw43_arch_poll
        pico_btstack_ble
        pico_btstack_classic
        pico_btstack_cyw43
    )
    joypad_target_common(joypad_btusb2usb)
    pico_enable_stdio_usb(joypad_btusb2usb 0)
    pico_enable_stdio_uart(joypad_btusb2usb 1)

    # --- USB to BLE (Pico W / Pico 2 W: PIO-USB host + CYW43 BLE peripheral output) ---
    # USB controllers via PIO-USB host → BLE gamepad output via HOGP peripheral.
    # Uses CYW43 Bluetooth for BLE advertising + GATT server (hids_device).
    # Same PIO/DMA resource management as btusb2usb.
    add_executable(joypad_usb2ble)
    target_compile_definitions(joypad_usb2ble PRIVATE
        CONFIG_USB2BLE=1
        CONFIG_USB=1
        CONFIG_NO_NEOPIXEL=1
        PICO_DEFAULT_PIO_USB_DP_PIN=16
        BOARD_TUH_RHPORT=1
        USE_BOOTSEL_BUTTON=1
        CYW43_LWIP=0
        BTSTACK_USE_CYW43=1
        ENABLE_BTSTACK=1
    )
    target_sources(joypad_usb2ble PUBLIC
        ${COMMON_SOURCES}
        ${USB_DEVICE_SOURCES}
        ${BTHID_DEVICE_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/generic/hid_parser.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack/btstack_host.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack/bt_device_db.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/transport/bt_transport.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/transport/bt_transport_cyw43.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/ble_output/ble_output.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/ble_output/ble_output_keyboard.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/ble_output/ble_output_mouse.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/ble_output/ble_output_xbox.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/ble_output/ble_nus.c
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2ble/app.c
    )
    target_include_directories(joypad_usb2ble PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2ble
        ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
        ${CMAKE_CURRENT_SOURCE_DIR}/bt
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/ble_output
        ${BTSTACK_INCLUDES}
    )
    # Xbox BLE GATT database — compiled as a separate library to avoid
    # profile_data symbol collision with the standard composite GATT.
    add_library(usb2ble_xbox_gatt OBJECT
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/ble_output/ble_xbox_gatt_db.c
    )
    target_include_directories(usb2ble_xbox_gatt PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/ble_output
        ${BTSTACK_INCLUDES}
    )
    pico_btstack_make_gatt_header(usb2ble_xbox_gatt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bt/ble_output/ble_xbox.gatt)
    target_link_libraries(joypad_usb2ble PRIVATE
        ${COMMON_LIBRARIES}
        tinyusb_device
        tinyusb_pico_pio_usb
        pico_cyw43_arch_poll
        pico_btstack_ble
        pico_btstack_classic
        pico_btstack_cyw43
        usb2ble_xbox_gatt
    )
    pico_btstack_make_gatt_header(joypad_usb2ble PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bt/ble_output/ble_gamepad.gatt)
    joypad_target_common(joypad_usb2ble)
    pico_enable_stdio_usb(joypad_usb2ble 0)
    pico_enable_stdio_uart(joypad_usb2ble 1)

    # --- ControllerBTUSB (Pico W / Pico 2 W: JoyWing sensor + BLE peripheral + USB) ---
    # Sensor inputs (JoyWing seesaw I2C) → BLE gamepad output (HOGP) + USB device.
    # Uses CYW43 Bluetooth for BLE advertising + GATT server (hids_device).
    add_executable(joypad_controller_btusb)
    target_compile_definitions(joypad_controller_btusb PRIVATE
        CONFIG_CONTROLLER_BTUSB=1
        CONFIG_USB=1
        DISABLE_USB_HOST=1
        USE_BOOTSEL_BUTTON=1
        CYW43_LWIP=0
        BTSTACK_USE_CYW43=1
        ENABLE_BTSTACK=1
        REQUIRE_BLE_OUTPUT=1
        SENSOR_JOYWING=1
    )
    target_sources(joypad_controller_btusb PUBLIC
        ${CORE_SOURCES}
        ${USB_DEVICE_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/stubs_peripheral.c
        ${CMAKE_CURRENT_SOURCE_DIR}/drivers/joywing/joywing_input.c
        ${CMAKE_CURRENT_SOURCE_DIR}/drivers/seesaw/seesaw.c
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/rp2040/platform_i2c_rp2040.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/ble_output/ble_output.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/ble_output/ble_output_keyboard.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/ble_output/ble_output_mouse.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/ble_output/ble_output_xbox.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/ble_output/ble_nus.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/transport/bt_transport.c
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/transport/bt_transport_cyw43.c
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/controller_btusb/app.c
    )
    target_include_directories(joypad_controller_btusb PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/controller_btusb
        ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
        ${CMAKE_CURRENT_SOURCE_DIR}/bt
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/ble_output
        ${BTSTACK_INCLUDES}
    )
    # Xbox BLE GATT database — separate library to avoid profile_data collision
    add_library(controller_btusb_xbox_gatt OBJECT
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/ble_output/ble_xbox_gatt_db.c
    )
    target_include_directories(controller_btusb_xbox_gatt PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/ble_output
        ${BTSTACK_INCLUDES}
    )
    pico_btstack_make_gatt_header(controller_btusb_xbox_gatt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bt/ble_output/ble_xbox.gatt)
    target_link_libraries(joypad_controller_btusb PRIVATE
        pico_stdlib
        pico_multicore
        pico_rand
        hardware_i2c
        pico_cyw43_arch_poll
        pico_btstack_ble
        pico_btstack_classic
        pico_btstack_cyw43
        tinyusb_device
        tinyusb_board
        controller_btusb_xbox_gatt
    )
    pico_btstack_make_gatt_header(joypad_controller_btusb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bt/ble_output/ble_gamepad.gatt)
    joypad_target_common(joypad_controller_btusb)
    pico_enable_stdio_usb(joypad_controller_btusb 0)
    pico_enable_stdio_uart(joypad_controller_btusb 1)

    # --- WiFi to USB (Pico W / Pico 2 W WiFi AP mode) ---
    add_executable(joypad_wifi2usb)
    target_compile_definitions(joypad_wifi2usb PRIVATE
        CONFIG_WIFI2USB=1
        CONFIG_USB=1
        DISABLE_USB_HOST=1
        USE_BOOTSEL_BUTTON=1
        CYW43_LWIP=1
        # WiFi AP mode + BLE beacon for iOS discovery
        PICO_CYW43_ARCH_POLL=1
        BTSTACK_USE_CYW43=1
    )
    target_sources(joypad_wifi2usb PUBLIC
        ${CORE_SOURCES}
        ${USB_DEVICE_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/wifi/jocp/jocp_input.c
        ${CMAKE_CURRENT_SOURCE_DIR}/wifi/jocp/wifi_transport.c
        ${CMAKE_CURRENT_SOURCE_DIR}/wifi/jocp/dhcpserver.c
        ${CMAKE_CURRENT_SOURCE_DIR}/wifi/ble_beacon.c
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/wifi2usb/app.c
    )
    target_include_directories(joypad_wifi2usb PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/wifi2usb
        ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
        ${CMAKE_CURRENT_SOURCE_DIR}/wifi/jocp
        ${CMAKE_CURRENT_SOURCE_DIR}/wifi
        ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack
    )
    target_link_libraries(joypad_wifi2usb PRIVATE
        pico_stdlib
        pico_multicore
        pico_rand
        pico_cyw43_arch_lwip_poll
        pico_btstack_cyw43
        pico_btstack_ble
        pico_btstack_classic
        pico_btstack_flash_bank
        tinyusb_device
        tinyusb_board
    )
    joypad_target_common(joypad_wifi2usb)
    pico_enable_stdio_usb(joypad_wifi2usb 0)
    pico_enable_stdio_uart(joypad_wifi2usb 1)
endif()

# --- SNES to USB ---
add_executable(joypad_snes2usb)
target_compile_definitions(joypad_snes2usb PRIVATE CONFIG_SNES2USB=1 CONFIG_USB=1 DISABLE_USB_HOST=1)
target_sources(joypad_snes2usb PUBLIC ${CORE_SOURCES} ${USB_DEVICE_SOURCES}
    ${SNES_HOST_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/snes2usb/app.c
)
target_include_directories(joypad_snes2usb PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/snes2usb
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/snes
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/SNESpad/src
)
target_link_libraries(joypad_snes2usb PRIVATE pico_stdlib pico_multicore hardware_pio pico_rand tinyusb_device tinyusb_board)
joypad_target_common(joypad_snes2usb)

# --- N642USB (N64 -> USB HID) ---
add_executable(joypad_n642usb)
target_compile_definitions(joypad_n642usb PRIVATE CONFIG_N642USB=1 CONFIG_USB=1 DISABLE_USB_HOST=1 N64_PIN_DATA=29)
target_sources(joypad_n642usb PUBLIC ${CORE_SOURCES} ${USB_DEVICE_SOURCES}
    ${N64_HOST_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/n642usb/app.c
)
target_include_directories(joypad_n642usb PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/n642usb
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/n64
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/include
)
target_link_libraries(joypad_n642usb PRIVATE pico_stdlib pico_multicore hardware_pio pico_rand tinyusb_device tinyusb_board)
joypad_target_common(joypad_n642usb)
pico_generate_pio_header(joypad_n642usb ${CMAKE_CURRENT_LIST_DIR}/lib/joybus-pio/src/joybus.pio)

# --- NUON2USB (Nuon -> USB HID) ---
# TODO: uncomment when nuon_host.c and apps/nuon2usb/app.c are implemented
# add_executable(joypad_nuon2usb)
# target_compile_definitions(joypad_nuon2usb PRIVATE CONFIG_NUON2USB=1 CONFIG_USB=1 DISABLE_USB_HOST=1 NUON_PIN_DATA=2)
# target_sources(joypad_nuon2usb PUBLIC ${CORE_SOURCES} ${USB_DEVICE_SOURCES}
#     ${NUON_HOST_SOURCES}
#     ${CMAKE_CURRENT_SOURCE_DIR}/apps/nuon2usb/app.c
# )
# target_include_directories(joypad_nuon2usb PUBLIC
#     ${CMAKE_CURRENT_SOURCE_DIR}/apps/nuon2usb
#     ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
#     ${CMAKE_CURRENT_SOURCE_DIR}/native/host/nuon
#     ${CMAKE_CURRENT_SOURCE_DIR}/native/device/nuon
# )
# target_link_libraries(joypad_nuon2usb PRIVATE pico_stdlib pico_multicore hardware_pio pico_rand tinyusb_device tinyusb_board)
# joypad_target_common(joypad_nuon2usb)
# pico_generate_pio_header(joypad_nuon2usb ${CMAKE_CURRENT_LIST_DIR}/native/device/nuon/polyface_read.pio)
# pico_generate_pio_header(joypad_nuon2usb ${CMAKE_CURRENT_LIST_DIR}/native/device/nuon/polyface_send.pio)

# --- GC2USB (GameCube -> USB HID) ---
add_executable(joypad_gc2usb)
target_compile_definitions(joypad_gc2usb PRIVATE CONFIG_GC2USB=1 CONFIG_USB=1 DISABLE_USB_HOST=1 GC_PIN_DATA=29
    PICO_DEFAULT_UART=0
    PICO_DEFAULT_UART_TX_PIN=12
    PICO_DEFAULT_UART_RX_PIN=13
)
target_sources(joypad_gc2usb PUBLIC ${CORE_SOURCES} ${USB_DEVICE_SOURCES}
    ${GC_HOST_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/gc2usb/app.c
)
target_include_directories(joypad_gc2usb PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/gc2usb
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/gc
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/include
)
target_link_libraries(joypad_gc2usb PRIVATE pico_stdlib pico_multicore hardware_pio pico_rand tinyusb_device tinyusb_board)
joypad_target_common(joypad_gc2usb)
pico_generate_pio_header(joypad_gc2usb ${CMAKE_CURRENT_LIST_DIR}/lib/joybus-pio/src/joybus.pio)

# --- NEGEO2USB (NEOGEO -> USB HID) ---
add_executable(joypad_neogeo2usb)
target_compile_definitions(joypad_neogeo2usb PRIVATE CONFIG_NEOGEO2USB=1 CONFIG_USB=1 DISABLE_USB_HOST=1 BUTTON_USER_GPIO=${BOARD_BUTTON_GPIO})
target_sources(joypad_neogeo2usb PUBLIC ${CORE_SOURCES} ${USB_DEVICE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/arcade/arcade_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/neogeo2usb/app.c
)
target_include_directories(joypad_neogeo2usb PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/neogeo2usb
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/arcade
)
target_link_libraries(joypad_neogeo2usb PRIVATE pico_stdlib pico_multicore hardware_pio pico_rand tinyusb_device tinyusb_board)
joypad_target_common(joypad_neogeo2usb)

# NEGEO2USB — RP2040-Zero (WS2812 on GP16, auto from board def)
add_executable(joypad_neogeo2usb_rp2040zero)
target_compile_definitions(joypad_neogeo2usb_rp2040zero PRIVATE
    CONFIG_NEOGEO2USB=1
    CONFIG_USB=1
    DISABLE_USB_HOST=1
    USE_BOOTSEL_BUTTON=1
    PICO_RP2040_ZERO_BUILD=1
)
target_sources(joypad_neogeo2usb_rp2040zero PUBLIC ${CORE_SOURCES} ${USB_DEVICE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/arcade/arcade_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/neogeo2usb/app.c
)
target_include_directories(joypad_neogeo2usb_rp2040zero PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/neogeo2usb
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/arcade
)
target_link_libraries(joypad_neogeo2usb_rp2040zero PRIVATE pico_stdlib pico_multicore hardware_pio pico_rand tinyusb_device tinyusb_board)
joypad_target_common(joypad_neogeo2usb_rp2040zero)

# --- NES2USB (NES -> USB HID) ---
add_executable(joypad_nes2usb)
target_compile_definitions(joypad_nes2usb PRIVATE CONFIG_NES2USB=1 CONFIG_USB=1 DISABLE_USB_HOST=1)
target_sources(joypad_nes2usb PUBLIC ${CORE_SOURCES} ${USB_DEVICE_SOURCES}
    ${NES_HOST_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/nes2usb/app.c
)
target_include_directories(joypad_nes2usb PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/nes2usb
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/nes
)
target_link_libraries(joypad_nes2usb PRIVATE pico_stdlib pico_multicore hardware_pio pico_rand tinyusb_device tinyusb_board)
joypad_target_common(joypad_nes2usb)
pico_generate_pio_header(joypad_nes2usb ${CMAKE_CURRENT_LIST_DIR}/native/host/nes/nes_host.pio)


# ============================================================================
# CONTROLLER APPS (GPIO/pad input -> USB output)
# ============================================================================

set(CONTROLLER_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/controller
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
    ${CMAKE_CURRENT_SOURCE_DIR}/pad
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/uart
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/uart
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host
)

# --- Fisher Price V1 (button-only mod) ---
add_executable(joypad_controller_fisherprice_v1)
target_compile_definitions(joypad_controller_fisherprice_v1 PRIVATE
    CONFIG_CONTROLLER=1 CONFIG_USB=1 CONTROLLER_TYPE_FISHERPRICE_V1=1 DISABLE_USB_HOST=1 BUTTON_USER_GPIO=${BOARD_BUTTON_GPIO}
)
target_sources(joypad_controller_fisherprice_v1 PUBLIC ${CORE_SOURCES} ${USB_DEVICE_SOURCES} ${CONTROLLER_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/controller/app.c
)
target_include_directories(joypad_controller_fisherprice_v1 PUBLIC ${CONTROLLER_INCLUDES})
target_link_libraries(joypad_controller_fisherprice_v1 PRIVATE ${CONTROLLER_LIBRARIES})
joypad_target_common(joypad_controller_fisherprice_v1)

# --- Fisher Price V2 (analog stick + shoulders mod) ---
add_executable(joypad_controller_fisherprice_v2)
target_compile_definitions(joypad_controller_fisherprice_v2 PRIVATE
    CONFIG_CONTROLLER=1 CONFIG_USB=1 CONTROLLER_TYPE_FISHERPRICE_V2=1 DISABLE_USB_HOST=1 BUTTON_USER_GPIO=${BOARD_BUTTON_GPIO}
)
target_sources(joypad_controller_fisherprice_v2 PUBLIC ${CORE_SOURCES} ${USB_DEVICE_SOURCES} ${CONTROLLER_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/controller/app.c
)
target_include_directories(joypad_controller_fisherprice_v2 PUBLIC ${CONTROLLER_INCLUDES})
target_link_libraries(joypad_controller_fisherprice_v2 PRIVATE ${CONTROLLER_LIBRARIES})
joypad_target_common(joypad_controller_fisherprice_v2)

# --- Alpakka ---
add_executable(joypad_controller_alpakka)
target_compile_definitions(joypad_controller_alpakka PRIVATE
    CONFIG_CONTROLLER=1 CONFIG_USB=1 CONTROLLER_TYPE_ALPAKKA=1 DISABLE_USB_HOST=1 BUTTON_USER_GPIO=${BOARD_BUTTON_GPIO}
)
target_sources(joypad_controller_alpakka PUBLIC ${CORE_SOURCES} ${USB_DEVICE_SOURCES} ${CONTROLLER_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/controller/app.c
)
target_include_directories(joypad_controller_alpakka PUBLIC ${CONTROLLER_INCLUDES})
target_link_libraries(joypad_controller_alpakka PRIVATE ${CONTROLLER_LIBRARIES})
joypad_target_common(joypad_controller_alpakka)

# --- MacroPad ---
add_executable(joypad_controller_macropad)
target_compile_definitions(joypad_controller_macropad PRIVATE
    CONFIG_CONTROLLER=1 CONFIG_USB=1 CONTROLLER_TYPE_MACROPAD=1 DISABLE_USB_HOST=1 BUTTON_USER_GPIO=${BOARD_BUTTON_GPIO}
    ADAFRUIT_MACROPAD_RP2040=1 WS2812_NUM_PIXELS=12
    I2C_PEER_ENABLED=1
)
target_sources(joypad_controller_macropad PUBLIC ${CORE_SOURCES} ${USB_DEVICE_SOURCES} ${CONTROLLER_SOURCES}
    ${I2C_PEER_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/controller/app.c
)
target_include_directories(joypad_controller_macropad PUBLIC ${CONTROLLER_INCLUDES})
target_link_libraries(joypad_controller_macropad PRIVATE ${CONTROLLER_LIBRARIES} pico_i2c_slave)
joypad_target_common(joypad_controller_macropad)

# --- ControllerBTUSB (Feather RP2040, USB-only, JoyWing + OLED + Joy) ---
# No BLE on Feather RP2040 — USB device output only.
# JoyWing + OLED share I2C1 on STEMMA QT (SDA=2, SCL=3).
add_executable(joypad_controller_btusb_feather_rp2040)
target_compile_definitions(joypad_controller_btusb_feather_rp2040 PRIVATE
    DISABLE_USB_HOST=1
    BOARD_FEATHER_RP2040=1
    USE_BOOTSEL_BUTTON=1
    REQUIRE_BLE_OUTPUT=0
    SENSOR_JOYWING=1
    PICO_DEFAULT_UART=0
    PICO_DEFAULT_UART_TX_PIN=0
    PICO_DEFAULT_UART_RX_PIN=1
    # FeatherWing OLED 128x64 (SH1107) over I2C on STEMMA QT
    OLED_I2C_INST=1
    OLED_I2C_SDA_PIN=2
    OLED_I2C_SCL_PIN=3
    OLED_I2C_ADDR=0x3C
    OLED_BUTTON_A_PIN=9
    OLED_BUTTON_B_PIN=6
    OLED_BUTTON_C_PIN=5
)
target_sources(joypad_controller_btusb_feather_rp2040 PUBLIC
    ${CORE_SOURCES}
    ${USB_DEVICE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/drivers/joywing/joywing_input.c
    ${CMAKE_CURRENT_SOURCE_DIR}/drivers/seesaw/seesaw.c
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/rp2040/platform_i2c_rp2040.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/display/display.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/display/display_i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/display/joy_anim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/controller_btusb/app.c
)
target_include_directories(joypad_controller_btusb_feather_rp2040 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/controller_btusb
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
)
target_link_libraries(joypad_controller_btusb_feather_rp2040 PRIVATE
    pico_stdlib pico_multicore hardware_pio hardware_i2c pico_rand tinyusb_device tinyusb_board
)
joypad_target_common(joypad_controller_btusb_feather_rp2040)
pico_enable_stdio_uart(joypad_controller_btusb_feather_rp2040 1)

# ============================================================================
# DEBUG BUILD CONFIGURATION
# ============================================================================

if(CMAKE_BUILD_TYPE MATCHES Debug)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Og")
else()
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
endif()
