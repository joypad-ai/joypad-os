cmake_minimum_required(VERSION 3.12)

# Pull in SDK from local submodule (must be before project)
set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/pico-sdk)
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(usbretro C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

set(FAMILY rp2040)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/sony/sony_ds3.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/sony/sony_ds4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/sony/sony_ds5.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/sony/sony_psc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/hori/hori_horipad.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/hori/hori_pokken.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/logitech/logitech_wingman.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/sega/sega_astrocity.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/xinput/xinput.c
)

# Bluetooth sources (requires BTstack)
set(BT_HOST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/btd/hci_transport_h2_tinyusb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/transport/bt_transport.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/transport/bt_transport_usb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/bthid.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/bthid_registry.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/generic/bthid_gamepad.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/vendors/sony/ds3_bt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/vendors/sony/ds4_bt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/vendors/sony/ds5_bt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/vendors/nintendo/switch_pro_bt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/vendors/microsoft/xbox_bt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/vendors/microsoft/xbox_ble.c
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
    # BLE GATT services
    ${PICO_BTSTACK_PATH}/src/ble/gatt-service/hids_client.c
    # Classic (for HID Host)
    ${PICO_BTSTACK_PATH}/src/classic/sdp_client.c
    ${PICO_BTSTACK_PATH}/src/classic/sdp_server.c
    ${PICO_BTSTACK_PATH}/src/classic/sdp_util.c
    ${PICO_BTSTACK_PATH}/src/classic/device_id_server.c
    ${PICO_BTSTACK_PATH}/src/classic/hid_host.c
    ${PICO_BTSTACK_PATH}/src/classic/btstack_link_key_db_memory.c
    # 3rd party
    ${PICO_BTSTACK_PATH}/3rd-party/micro-ecc/uECC.c
    ${PICO_BTSTACK_PATH}/3rd-party/rijndael/rijndael.c
    # Platform
    ${PICO_BTSTACK_PATH}/platform/embedded/btstack_run_loop_embedded.c
    ${PICO_BTSTACK_PATH}/platform/embedded/hci_dump_embedded_stdout.c
)

# USB Device sources (for USB output apps)
set(USB_DEVICE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/usbd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/cdc/cdc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xid.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xinput.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xbone.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/xgip_protocol.c
)

# SNES host sources
set(SNES_HOST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/snes/snes_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/SNESpad/src/snespad.c
)

# Controller app common sources
set(CONTROLLER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/pad/pad_input.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/speaker/speaker.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/display/display.c
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/uart/uart_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/uart/uart_host.c
)

# Common sources (core + USB host)
set(COMMON_SOURCES ${CORE_SOURCES} ${USB_HOST_SOURCES})

# Common libraries for USB host apps
set(COMMON_LIBRARIES pico_stdlib pico_multicore hardware_pio tinyusb_host tinyusb_board xinput_host)

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
)

# Additional BTstack sources (beyond BT_HOST_SOURCES and BTSTACK_SOURCES)
set(BTSTACK_EXTRA_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/btstack/btstack_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/btd/btstack_hal.c
)

# Controller libraries
set(CONTROLLER_LIBRARIES pico_stdlib pico_multicore hardware_pio hardware_adc hardware_i2c hardware_pwm hardware_spi tinyusb_device tinyusb_board)

# ============================================================================
# HELPER FUNCTIONS
# ============================================================================

# Common setup for all targets
function(usbretro_target_common TARGET)
    target_compile_options(${TARGET} PRIVATE -O3)
    pico_add_extra_outputs(${TARGET})
    pico_generate_pio_header(${TARGET} ${CMAKE_CURRENT_LIST_DIR}/core/services/leds/neopixel/ws2812.pio)
    target_include_directories(${TARGET} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/common)
endfunction()

# Add BTstack support to a target
function(usbretro_add_btstack TARGET)
    target_compile_definitions(${TARGET} PRIVATE ${BTSTACK_DEFINITIONS})
    target_sources(${TARGET} PRIVATE
        ${BT_HOST_SOURCES}
        ${BTSTACK_SOURCES}
        ${BTSTACK_EXTRA_SOURCES}
    )
    target_include_directories(${TARGET} PUBLIC ${BTSTACK_INCLUDES})
endfunction()

# USB host console app (USB input -> console output)
function(usbretro_console_app TARGET CONFIG_NAME APP_DIR DEVICE_DIR)
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
    usbretro_target_common(${TARGET})
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
add_executable(usbretro_pce)
target_compile_definitions(usbretro_pce PRIVATE CONFIG_PCE=1)
if (PICO_BOARD STREQUAL "pico")
    target_compile_definitions(usbretro_pce PRIVATE RPI_PICO_BUILD=1)
endif()
target_sources(usbretro_pce PUBLIC ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/pcengine/pcengine_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2pce/app.c
)
target_include_directories(usbretro_pce PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2pce
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/pcengine
)
target_link_libraries(usbretro_pce PRIVATE ${COMMON_LIBRARIES})
usbretro_target_common(usbretro_pce)
usbretro_add_btstack(usbretro_pce)  # TODO: BTstack causes boot crash on console targets
pico_generate_pio_header(usbretro_pce ${CMAKE_CURRENT_LIST_DIR}/native/device/pcengine/plex.pio)
pico_generate_pio_header(usbretro_pce ${CMAKE_CURRENT_LIST_DIR}/native/device/pcengine/clock.pio)
pico_generate_pio_header(usbretro_pce ${CMAKE_CURRENT_LIST_DIR}/native/device/pcengine/select.pio)

# --- GameCube ---
add_executable(usbretro_ngc)
target_compile_definitions(usbretro_ngc PRIVATE CONFIG_NGC=1)
target_sources(usbretro_ngc PUBLIC ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/gamecube/gamecube_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/src/joybus.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/src/GamecubeConsole.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2gc/app.c
)
target_include_directories(usbretro_ngc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2gc
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/gamecube
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/include
)
target_link_libraries(usbretro_ngc PRIVATE ${COMMON_LIBRARIES} hardware_flash)
usbretro_target_common(usbretro_ngc)
usbretro_add_btstack(usbretro_ngc)  # Re-enabled for investigation
pico_generate_pio_header(usbretro_ngc ${CMAKE_CURRENT_LIST_DIR}/lib/joybus-pio/src/joybus.pio)

# --- Nuon ---
add_executable(usbretro_nuon)
target_compile_definitions(usbretro_nuon PRIVATE CONFIG_NUON=1)
target_sources(usbretro_nuon PUBLIC ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/nuon/nuon_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2nuon/app.c
)
target_include_directories(usbretro_nuon PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2nuon
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/nuon
)
target_link_libraries(usbretro_nuon PRIVATE ${COMMON_LIBRARIES} pico_bit_ops)
usbretro_target_common(usbretro_nuon)
# usbretro_add_btstack(usbretro_nuon)  # TODO: BTstack causes boot crash on console targets
pico_generate_pio_header(usbretro_nuon ${CMAKE_CURRENT_LIST_DIR}/native/device/nuon/polyface_read.pio)
pico_generate_pio_header(usbretro_nuon ${CMAKE_CURRENT_LIST_DIR}/native/device/nuon/polyface_send.pio)

# --- Loopy ---
add_executable(usbretro_loopy)
target_compile_definitions(usbretro_loopy PRIVATE CONFIG_LOOPY=1)
target_sources(usbretro_loopy PUBLIC ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/loopy/loopy_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2loopy/app.c
)
target_include_directories(usbretro_loopy PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2loopy
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/loopy
)
target_link_libraries(usbretro_loopy PRIVATE ${COMMON_LIBRARIES} pico_bit_ops)
usbretro_target_common(usbretro_loopy)
# usbretro_add_btstack(usbretro_loopy)  # TODO: BTstack causes boot crash on console targets
pico_generate_pio_header(usbretro_loopy ${CMAKE_CURRENT_LIST_DIR}/native/device/loopy/loopy.pio)

# --- 3DO ---
add_executable(usbretro_3do)
target_compile_definitions(usbretro_3do PRIVATE CONFIG_3DO=1)
target_sources(usbretro_3do PUBLIC ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/3do/3do_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb23do/app.c
)
target_include_directories(usbretro_3do PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb23do
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/3do
)
target_link_libraries(usbretro_3do PRIVATE ${COMMON_LIBRARIES} hardware_dma)
usbretro_target_common(usbretro_3do)
# usbretro_add_btstack(usbretro_3do)  # TODO: BTstack causes boot crash on console targets
pico_generate_pio_header(usbretro_3do ${CMAKE_CURRENT_LIST_DIR}/native/device/3do/sampling.pio)
pico_generate_pio_header(usbretro_3do ${CMAKE_CURRENT_LIST_DIR}/native/device/3do/output.pio)

# --- SNES to 3DO ---
add_executable(usbretro_snes3do)
target_compile_definitions(usbretro_snes3do PRIVATE CONFIG_SNES3DO=1)
target_sources(usbretro_snes3do PUBLIC ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/3do/3do_device.c
    ${SNES_HOST_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/snes23do/app.c
)
target_include_directories(usbretro_snes3do PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/snes23do
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/3do
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/snes
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/SNESpad/src
)
target_link_libraries(usbretro_snes3do PRIVATE ${COMMON_LIBRARIES} hardware_dma)
usbretro_target_common(usbretro_snes3do)
# usbretro_add_btstack(usbretro_snes3do)  # TODO: BTstack causes boot crash on console targets
pico_generate_pio_header(usbretro_snes3do ${CMAKE_CURRENT_LIST_DIR}/native/device/3do/sampling.pio)
pico_generate_pio_header(usbretro_snes3do ${CMAKE_CURRENT_LIST_DIR}/native/device/3do/output.pio)

# --- UART Bridge ---
add_executable(usbretro_uart)
target_compile_definitions(usbretro_uart PRIVATE CONFIG_UART=1)
target_sources(usbretro_uart PUBLIC ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/uart/uart_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2uart/app.c
)
target_include_directories(usbretro_uart PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2uart
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/uart
)
target_link_libraries(usbretro_uart PRIVATE ${COMMON_LIBRARIES} hardware_uart)
usbretro_target_common(usbretro_uart)
# usbretro_add_btstack(usbretro_uart)  # TODO: BTstack causes boot crash on console targets

# ============================================================================
# USB HOST TO USB DEVICE APPS (USB input -> USB output)
# ============================================================================

# --- USB to USB (Feather USB Host) ---
add_executable(usbretro_usb)
target_compile_definitions(usbretro_usb PRIVATE
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
target_sources(usbretro_usb PUBLIC
    ${COMMON_SOURCES}
    ${USB_DEVICE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb/app.c
)
target_include_directories(usbretro_usb PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
)
target_link_libraries(usbretro_usb PRIVATE ${COMMON_LIBRARIES} tinyusb_device tinyusb_pico_pio_usb)
usbretro_target_common(usbretro_usb)
usbretro_add_btstack(usbretro_usb)
pico_enable_stdio_uart(usbretro_usb 1)

# --- USB to USB (RP2040-Zero) ---
add_executable(usbretro_usb_rp2040zero)
target_compile_definitions(usbretro_usb_rp2040zero PRIVATE
    CONFIG_USB=1
    CONFIG_PIO_USB_DP_PIN=10
    PICO_DEFAULT_PIO_USB_DP_PIN=10
    BOARD_TUH_RHPORT=1
    WS2812_PIN=16
    USE_BOOTSEL_BUTTON=1
)
target_sources(usbretro_usb_rp2040zero PUBLIC ${COMMON_SOURCES} ${USB_DEVICE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb/app.c
)
target_include_directories(usbretro_usb_rp2040zero PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
)
target_link_libraries(usbretro_usb_rp2040zero PRIVATE ${COMMON_LIBRARIES} tinyusb_device tinyusb_pico_pio_usb)
usbretro_target_common(usbretro_usb_rp2040zero)
# usbretro_add_btstack(usbretro_usb_rp2040zero)  # TODO: BTstack causes boot crash - investigate

# --- SNES to USB ---
add_executable(usbretro_snes2usb)
target_compile_definitions(usbretro_snes2usb PRIVATE CONFIG_SNES2USB=1 CONFIG_USB=1 DISABLE_USB_HOST=1)
target_sources(usbretro_snes2usb PUBLIC ${CORE_SOURCES} ${USB_DEVICE_SOURCES}
    ${SNES_HOST_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/snes2usb/app.c
)
target_include_directories(usbretro_snes2usb PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/snes2usb
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/snes
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/SNESpad/src
)
target_link_libraries(usbretro_snes2usb PRIVATE pico_stdlib pico_multicore hardware_pio tinyusb_device tinyusb_board)
usbretro_target_common(usbretro_snes2usb)

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

# --- Fisher Price ---
add_executable(usbretro_controller_fisherprice)
target_compile_definitions(usbretro_controller_fisherprice PRIVATE
    CONFIG_CONTROLLER=1 CONTROLLER_TYPE_FISHERPRICE=1 DISABLE_USB_HOST=1 BUTTON_USER_GPIO=${BOARD_BUTTON_GPIO}
)
target_sources(usbretro_controller_fisherprice PUBLIC ${CORE_SOURCES} ${USB_DEVICE_SOURCES} ${CONTROLLER_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/controller/app.c
)
target_include_directories(usbretro_controller_fisherprice PUBLIC ${CONTROLLER_INCLUDES})
target_link_libraries(usbretro_controller_fisherprice PRIVATE ${CONTROLLER_LIBRARIES})
usbretro_target_common(usbretro_controller_fisherprice)

# --- Fisher Price Analog ---
add_executable(usbretro_controller_fisherprice_analog)
target_compile_definitions(usbretro_controller_fisherprice_analog PRIVATE
    CONFIG_CONTROLLER=1 CONTROLLER_TYPE_FISHERPRICE_ANALOG=1 DISABLE_USB_HOST=1 BUTTON_USER_GPIO=${BOARD_BUTTON_GPIO}
)
target_sources(usbretro_controller_fisherprice_analog PUBLIC ${CORE_SOURCES} ${USB_DEVICE_SOURCES} ${CONTROLLER_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/controller/app.c
)
target_include_directories(usbretro_controller_fisherprice_analog PUBLIC ${CONTROLLER_INCLUDES})
target_link_libraries(usbretro_controller_fisherprice_analog PRIVATE ${CONTROLLER_LIBRARIES})
usbretro_target_common(usbretro_controller_fisherprice_analog)

# --- Alpakka ---
add_executable(usbretro_controller_alpakka)
target_compile_definitions(usbretro_controller_alpakka PRIVATE
    CONFIG_CONTROLLER=1 CONTROLLER_TYPE_ALPAKKA=1 DISABLE_USB_HOST=1 BUTTON_USER_GPIO=${BOARD_BUTTON_GPIO}
)
target_sources(usbretro_controller_alpakka PUBLIC ${CORE_SOURCES} ${USB_DEVICE_SOURCES} ${CONTROLLER_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/controller/app.c
)
target_include_directories(usbretro_controller_alpakka PUBLIC ${CONTROLLER_INCLUDES})
target_link_libraries(usbretro_controller_alpakka PRIVATE ${CONTROLLER_LIBRARIES})
usbretro_target_common(usbretro_controller_alpakka)

# --- MacroPad ---
add_executable(usbretro_controller_macropad)
target_compile_definitions(usbretro_controller_macropad PRIVATE
    CONFIG_CONTROLLER=1 CONTROLLER_TYPE_MACROPAD=1 DISABLE_USB_HOST=1 BUTTON_USER_GPIO=${BOARD_BUTTON_GPIO}
    ADAFRUIT_MACROPAD_RP2040=1 WS2812_NUM_PIXELS=12
)
target_sources(usbretro_controller_macropad PUBLIC ${CORE_SOURCES} ${USB_DEVICE_SOURCES} ${CONTROLLER_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/controller/app.c
)
target_include_directories(usbretro_controller_macropad PUBLIC ${CONTROLLER_INCLUDES})
target_link_libraries(usbretro_controller_macropad PRIVATE ${CONTROLLER_LIBRARIES})
usbretro_target_common(usbretro_controller_macropad)

# ============================================================================
# DEBUG BUILD CONFIGURATION
# ============================================================================

if(CMAKE_BUILD_TYPE MATCHES Debug)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Og")
else()
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
endif()
