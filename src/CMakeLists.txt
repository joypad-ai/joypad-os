cmake_minimum_required(VERSION 3.12)

# Pull in SDK from local submodule (must be before project)
set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/pico-sdk)
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(usbretro C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3") # Set optimization level to O3

#set(PICO_EXAMPLES_PATH ${PROJECT_SOURCE_DIR})

# Initialize the SDK
pico_sdk_init()

set(FAMILY rp2040)

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
    target_include_directories(tinyusb_pico_pio_usb INTERFACE
        ${PICO_PIO_USB_PATH}/src
    )
    target_link_libraries(tinyusb_pico_pio_usb INTERFACE
        hardware_pio
        hardware_dma
        pico_stdlib
    )
    target_compile_definitions(tinyusb_pico_pio_usb INTERFACE
        PIO_USB_USE_TINYUSB=1
        PIO_USB_DP_PIN_DEFAULT=16
    )
    # Generate PIO headers
    pico_generate_pio_header(tinyusb_pico_pio_usb ${PICO_PIO_USB_PATH}/src/usb_tx.pio)
    pico_generate_pio_header(tinyusb_pico_pio_usb ${PICO_PIO_USB_PATH}/src/usb_rx.pio)
endif()

add_executable(usbretro_pce) # For the usbretro_pce target
add_executable(usbretro_ngc) # For the usbretro_ngc target
add_executable(usbretro_nuon) # For the usbretro_nuon target
add_executable(usbretro_loopy) # For the usbretro_loopy target
add_executable(usbretro_3do) # For the usbretro_3do target
add_executable(usbretro_snes3do) # For the usbretro_snes3do target (Super3D0USB)
add_executable(usbretro_uart) # For the usbretro_uart target (USB2UART ESP32 bridge)
add_executable(usbretro_usb) # For the usbretro_usb target (USB2USB HID gamepad output)
add_executable(usbretro_usb_rp2040zero) # For the usbretro_usb target on rp2040zero (RP2040-Zero, D+ on GPIO 10)
add_executable(usbretro_snes2usb) # For the usbretro_snes2usb target (SNES→USB HID gamepad)
add_executable(usbretro_controller_fisherprice) # Controller app: Fisher Price (KB2040)
add_executable(usbretro_controller_fisherprice_analog) # Controller app: Fisher Price Analog (KB2040)
add_executable(usbretro_controller_alpakka) # Controller app: Alpakka (Pico)
add_executable(usbretro_controller_macropad) # Controller app: MacroPad (Adafruit MacroPad RP2040)

if (PICO_BOARD STREQUAL "pico")
    message(STATUS "PICO_BOARD is '${PICO_BOARD}', applying RPI_PICO_BUILD definition for usbretro_pce.")
    target_compile_definitions(usbretro_pce PRIVATE RPI_PICO_BUILD=1)
endif()

# Board-level user button GPIO definitions
# Each board has a different GPIO for the user/boot button
if (PICO_BOARD STREQUAL "adafruit_kb2040")
    set(BOARD_BUTTON_GPIO 11)
elseif (PICO_BOARD STREQUAL "adafruit_qtpy_rp2040")
    set(BOARD_BUTTON_GPIO 21)
elseif (PICO_BOARD STREQUAL "adafruit_macropad_rp2040")
    set(BOARD_BUTTON_GPIO 0)  # Rotary encoder button
elseif (PICO_BOARD STREQUAL "adafruit_feather_rp2040")
    set(BOARD_BUTTON_GPIO 7)
else()
    set(BOARD_BUTTON_GPIO 7)  # Default fallback
endif()
message(STATUS "Board ${PICO_BOARD}: BUTTON_USER_GPIO=${BOARD_BUTTON_GPIO}")

target_compile_definitions(usbretro_pce PRIVATE CONFIG_PCE=1)
target_compile_definitions(usbretro_ngc PRIVATE CONFIG_NGC=1)
target_compile_definitions(usbretro_nuon PRIVATE CONFIG_NUON=1)
target_compile_definitions(usbretro_loopy PRIVATE CONFIG_LOOPY=1)
target_compile_definitions(usbretro_3do PRIVATE CONFIG_3DO=1)
target_compile_definitions(usbretro_snes3do PRIVATE CONFIG_SNES3DO=1)
target_compile_definitions(usbretro_uart PRIVATE CONFIG_UART=1)
target_compile_definitions(usbretro_usb PRIVATE
    CONFIG_USB=1
    # PIO USB defines for TinyUSB family.c compatibility
    PICO_DEFAULT_PIO_USB_DP_PIN=16
    PICO_DEFAULT_PIO_USB_VBUSEN_PIN=18
    PICO_DEFAULT_PIO_USB_VBUSEN_STATE=1
    BOARD_TUH_RHPORT=1
    # CDC debug: 1=enabled (dev), 0=disabled (prod)
    USBR_CDC_DEBUG=1
    # UART debug on GPIO 0 (TX) and GPIO 1 (RX) - UART0 pins
    PICO_DEFAULT_UART=0
    PICO_DEFAULT_UART_TX_PIN=0
    PICO_DEFAULT_UART_RX_PIN=1
)
target_compile_definitions(usbretro_usb_rp2040zero PRIVATE
    CONFIG_USB=1
    # OGX-Mini (RP2040-Zero) PIO USB pins: D+ on GPIO 10, D- on GPIO 11
    CONFIG_PIO_USB_DP_PIN=10
    PICO_DEFAULT_PIO_USB_DP_PIN=10
    BOARD_TUH_RHPORT=1
    # NeoPixel on GPIO 16
    WS2812_PIN=16
)
target_compile_definitions(usbretro_snes2usb PRIVATE CONFIG_SNES2USB=1 CONFIG_USB=1 DISABLE_USB_HOST=1)
target_compile_definitions(usbretro_controller_fisherprice PRIVATE CONFIG_CONTROLLER=1 CONTROLLER_TYPE_FISHERPRICE=1 DISABLE_USB_HOST=1 BUTTON_USER_GPIO=${BOARD_BUTTON_GPIO})
target_compile_definitions(usbretro_controller_fisherprice_analog PRIVATE CONFIG_CONTROLLER=1 CONTROLLER_TYPE_FISHERPRICE_ANALOG=1 DISABLE_USB_HOST=1 BUTTON_USER_GPIO=${BOARD_BUTTON_GPIO})
target_compile_definitions(usbretro_controller_alpakka PRIVATE CONFIG_CONTROLLER=1 CONTROLLER_TYPE_ALPAKKA=1 DISABLE_USB_HOST=1 BUTTON_USER_GPIO=${BOARD_BUTTON_GPIO})
target_compile_definitions(usbretro_controller_macropad PRIVATE CONFIG_CONTROLLER=1 CONTROLLER_TYPE_MACROPAD=1 DISABLE_USB_HOST=1 BUTTON_USER_GPIO=${BOARD_BUTTON_GPIO} ADAFRUIT_MACROPAD_RP2040=1 WS2812_NUM_PIXELS=12)

target_compile_options(usbretro_pce PRIVATE -O3) # Set optimization level to O3 for your_target
target_compile_options(usbretro_ngc PRIVATE -O3) # Set optimization level to O3 for your_target
target_compile_options(usbretro_nuon PRIVATE -O3) # Set optimization level to O3 for your_target
target_compile_options(usbretro_loopy PRIVATE -O3) # Set optimization level to O3 for your_target
target_compile_options(usbretro_3do PRIVATE -O3) # Set optimization level to O3 for your_target
target_compile_options(usbretro_snes3do PRIVATE -O3) # Set optimization level to O3 for your_target
target_compile_options(usbretro_uart PRIVATE -O3) # Set optimization level to O3 for your_target
target_compile_options(usbretro_usb PRIVATE -O3) # Set optimization level to O3 for your_target
target_compile_options(usbretro_usb_rp2040zero PRIVATE -O3)
target_compile_options(usbretro_snes2usb PRIVATE -O3) # Set optimization level to O3 for your_target
target_compile_options(usbretro_controller_fisherprice PRIVATE -O3) # Set optimization level to O3 for your_target
target_compile_options(usbretro_controller_fisherprice_analog PRIVATE -O3)
target_compile_options(usbretro_controller_alpakka PRIVATE -O3) # Set optimization level to O3 for your_target
target_compile_options(usbretro_controller_macropad PRIVATE -O3) # Set optimization level to O3 for your_target

if(CMAKE_BUILD_TYPE MATCHES Debug)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Og")
else()
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
endif()

pico_add_extra_outputs(usbretro_pce)
pico_add_extra_outputs(usbretro_ngc)
pico_add_extra_outputs(usbretro_nuon)
pico_add_extra_outputs(usbretro_loopy)
pico_add_extra_outputs(usbretro_3do)
pico_add_extra_outputs(usbretro_snes3do)
pico_add_extra_outputs(usbretro_uart)
pico_add_extra_outputs(usbretro_usb)
pico_add_extra_outputs(usbretro_usb_rp2040zero)
pico_add_extra_outputs(usbretro_snes2usb)
pico_add_extra_outputs(usbretro_controller_fisherprice)
pico_add_extra_outputs(usbretro_controller_fisherprice_analog)
pico_add_extra_outputs(usbretro_controller_alpakka)
pico_add_extra_outputs(usbretro_controller_macropad)

# Enable UART stdio for usb2usb debugging (GPIO 0=TX, GPIO 1=RX)
pico_enable_stdio_uart(usbretro_usb 1)

pico_generate_pio_header(usbretro_pce ${CMAKE_CURRENT_LIST_DIR}/native/device/pcengine/plex.pio )
pico_generate_pio_header(usbretro_pce ${CMAKE_CURRENT_LIST_DIR}/native/device/pcengine/clock.pio )
pico_generate_pio_header(usbretro_pce ${CMAKE_CURRENT_LIST_DIR}/native/device/pcengine/select.pio )
pico_generate_pio_header(usbretro_pce ${CMAKE_CURRENT_LIST_DIR}/core/services/leds/neopixel/ws2812.pio )
pico_generate_pio_header(usbretro_ngc ${CMAKE_CURRENT_LIST_DIR}/lib/joybus-pio/src/joybus.pio )
pico_generate_pio_header(usbretro_ngc ${CMAKE_CURRENT_LIST_DIR}/core/services/leds/neopixel/ws2812.pio )
pico_generate_pio_header(usbretro_nuon ${CMAKE_CURRENT_LIST_DIR}/native/device/nuon/polyface_read.pio )
pico_generate_pio_header(usbretro_nuon ${CMAKE_CURRENT_LIST_DIR}/native/device/nuon/polyface_send.pio )
pico_generate_pio_header(usbretro_nuon ${CMAKE_CURRENT_LIST_DIR}/core/services/leds/neopixel/ws2812.pio )
pico_generate_pio_header(usbretro_loopy ${CMAKE_CURRENT_LIST_DIR}/native/device/loopy/loopy.pio )
pico_generate_pio_header(usbretro_loopy ${CMAKE_CURRENT_LIST_DIR}/core/services/leds/neopixel/ws2812.pio )
pico_generate_pio_header(usbretro_3do ${CMAKE_CURRENT_LIST_DIR}/native/device/3do/sampling.pio )
pico_generate_pio_header(usbretro_3do ${CMAKE_CURRENT_LIST_DIR}/native/device/3do/output.pio )
pico_generate_pio_header(usbretro_3do ${CMAKE_CURRENT_LIST_DIR}/core/services/leds/neopixel/ws2812.pio )
pico_generate_pio_header(usbretro_snes3do ${CMAKE_CURRENT_LIST_DIR}/native/device/3do/sampling.pio )
pico_generate_pio_header(usbretro_snes3do ${CMAKE_CURRENT_LIST_DIR}/native/device/3do/output.pio )
pico_generate_pio_header(usbretro_snes3do ${CMAKE_CURRENT_LIST_DIR}/core/services/leds/neopixel/ws2812.pio )
pico_generate_pio_header(usbretro_uart ${CMAKE_CURRENT_LIST_DIR}/core/services/leds/neopixel/ws2812.pio )
pico_generate_pio_header(usbretro_usb ${CMAKE_CURRENT_LIST_DIR}/core/services/leds/neopixel/ws2812.pio )
pico_generate_pio_header(usbretro_usb_rp2040zero ${CMAKE_CURRENT_LIST_DIR}/core/services/leds/neopixel/ws2812.pio )
pico_generate_pio_header(usbretro_snes2usb ${CMAKE_CURRENT_LIST_DIR}/core/services/leds/neopixel/ws2812.pio )
pico_generate_pio_header(usbretro_controller_fisherprice ${CMAKE_CURRENT_LIST_DIR}/core/services/leds/neopixel/ws2812.pio )
pico_generate_pio_header(usbretro_controller_fisherprice_analog ${CMAKE_CURRENT_LIST_DIR}/core/services/leds/neopixel/ws2812.pio )
pico_generate_pio_header(usbretro_controller_alpakka ${CMAKE_CURRENT_LIST_DIR}/core/services/leds/neopixel/ws2812.pio )
pico_generate_pio_header(usbretro_controller_macropad ${CMAKE_CURRENT_LIST_DIR}/core/services/leds/neopixel/ws2812.pio )

## SET TARGET SOURCES

# Core sources (no USB host - used by native-input apps like snes2usb)
set(CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/leds/leds.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/leds/neopixel/ws2812.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/storage/storage.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/storage/flash.c

    # Core Services
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/button/button.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/codes/codes.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/hotkeys/hotkeys.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/players/manager.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/players/feedback.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/profiles/profile.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/profiles/profile_indicator.c

    # Core Router System
    ${CMAKE_CURRENT_SOURCE_DIR}/core/router/router.c

    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
)

# USB Host sources (HID + X-input drivers)
set(USB_HOST_SOURCES
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
    # Unregistered drivers (handled by generic hid_parser):
    # - 8bitdo_neo.c, dragonrise.c, triple_adapter_v1.c, triple_adapter_v2.c

    # USB Host - X-input Protocol
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/xinput/xinput.c

    # USB Host - Bluetooth Dongle (BTD) Protocol
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/btd/btd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/btd/btd_linkkey.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/btd/l2cap.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/btd/btd_glue.c

    # Bluetooth Transport Layer
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/transport/bt_transport.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/transport/bt_transport_usb.c

    # Bluetooth HID Layer
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/bthid.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/bthid_registry.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/generic/bthid_gamepad.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/vendors/sony/ds4_bt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/vendors/sony/ds5_bt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/vendors/nintendo/switch_pro_bt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bt/bthid/devices/vendors/microsoft/xbox_bt.c

    # USB Host - Unified layer
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/usbh.c
)

# Common sources for most apps (core + USB host)
set(COMMON_SOURCES
    ${CORE_SOURCES}
    ${USB_HOST_SOURCES}
)

target_sources(usbretro_pce PUBLIC # For PCE
    ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/pcengine/pcengine_device.c
    # Phase 5: App layer
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2pce/app.c
)

target_sources(usbretro_ngc PUBLIC # For NGC
    ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/gamecube/gamecube_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/src/joybus.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/src/GamecubeConsole.c
    # Phase 5: App layer
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2gc/app.c
)

target_sources(usbretro_nuon PUBLIC # For NUON
    ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/nuon/nuon_device.c
    # Phase 5: App layer
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2nuon/app.c
)

target_sources(usbretro_loopy PUBLIC # For (Casio) LOOPY
    ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/loopy/loopy_device.c
    # Phase 5: App layer
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2loopy/app.c
)

target_sources(usbretro_3do PUBLIC # For 3DO
    ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/3do/3do_device.c
    # Phase 5: App layer
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb23do/app.c
)

target_sources(usbretro_snes3do PUBLIC # For SNES23DO (SNES→3DO)
    ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/3do/3do_device.c
    # Native SNES host driver
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/snes/snes_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/SNESpad/src/snespad.c
    # Phase 5: App layer
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/snes23do/app.c
)

target_sources(usbretro_uart PUBLIC # For UART (USB→UART ESP32 bridge)
    ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/uart/uart_device.c
    # Phase 5: App layer
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2uart/app.c
)

target_sources(usbretro_usb PUBLIC # For USB (USB→USB HID gamepad)
    ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/usbd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/cdc/cdc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xid.c  # Xbox Original XID class driver
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xinput.c  # Xbox 360 XInput class driver
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xbone.c  # Xbox One GIP class driver
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/xgip_protocol.c  # Xbox One GIP protocol
    # Phase 5: App layer
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb/app.c
)

target_sources(usbretro_usb_rp2040zero PUBLIC # For USB on rp2040zero (RP2040-Zero)
    ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/usbd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/cdc/cdc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xid.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xinput.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xbone.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/xgip_protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb/app.c
)

target_sources(usbretro_snes2usb PUBLIC # For SNES2USB (SNES→USB HID gamepad)
    ${CORE_SOURCES}  # No USB host - uses native SNES input
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/usbd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/cdc/cdc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xid.c  # Xbox Original XID class driver
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xinput.c  # Xbox 360 XInput class driver
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xbone.c  # Xbox One GIP class driver
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/xgip_protocol.c  # Xbox One GIP protocol
    # Native SNES host driver
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/snes/snes_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/SNESpad/src/snespad.c
    # Phase 5: App layer
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/snes2usb/app.c
)

target_sources(usbretro_controller_fisherprice PUBLIC # Controller app: Fisher Price
    ${CORE_SOURCES}  # No USB host - uses GPIO input
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/usbd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/cdc/cdc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xid.c  # Xbox Original XID class driver
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xinput.c  # Xbox 360 XInput class driver
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xbone.c  # Xbox One GIP class driver
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/xgip_protocol.c  # Xbox One GIP protocol
    # Pad input driver
    ${CMAKE_CURRENT_SOURCE_DIR}/pad/pad_input.c
    # Speaker driver for rumble feedback (no-op if not configured)
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/speaker/speaker.c
    # Display driver for OLED status (no-op if not configured)
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/display/display.c
    # UART host/device for linking controllers via QWIIC (no-op if not configured)
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/uart/uart_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/uart/uart_host.c
    # App layer
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/controller/app.c
)

target_sources(usbretro_controller_fisherprice_analog PUBLIC # Controller app: Fisher Price Analog
    ${CORE_SOURCES}  # No USB host - uses GPIO input
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/usbd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/cdc/cdc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xid.c  # Xbox Original XID class driver
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xinput.c  # Xbox 360 XInput class driver
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xbone.c  # Xbox One GIP class driver
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/xgip_protocol.c  # Xbox One GIP protocol
    # Pad input driver
    ${CMAKE_CURRENT_SOURCE_DIR}/pad/pad_input.c
    # Speaker driver for rumble feedback (no-op if not configured)
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/speaker/speaker.c
    # Display driver for OLED status (no-op if not configured)
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/display/display.c
    # UART host/device for linking controllers via QWIIC (no-op if not configured)
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/uart/uart_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/uart/uart_host.c
    # App layer
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/controller/app.c
)

target_sources(usbretro_controller_alpakka PUBLIC # Controller app: Alpakka
    ${CORE_SOURCES}  # No USB host - uses pad input
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/usbd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/cdc/cdc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xid.c  # Xbox Original XID class driver
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xinput.c  # Xbox 360 XInput class driver
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xbone.c  # Xbox One GIP class driver
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/xgip_protocol.c  # Xbox One GIP protocol
    # Pad input driver
    ${CMAKE_CURRENT_SOURCE_DIR}/pad/pad_input.c
    # Speaker driver for rumble feedback (no-op if not configured)
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/speaker/speaker.c
    # Display driver for OLED status (no-op if not configured)
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/display/display.c
    # UART host/device for linking controllers via QWIIC (no-op if not configured)
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/uart/uart_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/uart/uart_host.c
    # App layer
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/controller/app.c
)

target_sources(usbretro_controller_macropad PUBLIC # Controller app: MacroPad
    ${CORE_SOURCES}  # No USB host - uses pad input
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/usbd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/cdc/cdc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xid.c  # Xbox Original XID class driver
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xinput.c  # Xbox 360 XInput class driver
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/tud_xbone.c  # Xbox One GIP class driver
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd/xgip_protocol.c  # Xbox One GIP protocol
    # Pad input driver
    ${CMAKE_CURRENT_SOURCE_DIR}/pad/pad_input.c
    # Speaker driver for rumble feedback
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/speaker/speaker.c
    # Display driver for OLED status
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/display/display.c
    # UART host/device for linking controllers via QWIIC
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/uart/uart_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/uart/uart_host.c
    # App layer
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/controller/app.c
)

## SET TARGET INCLUDE DIRS
target_include_directories(usbretro_pce PUBLIC # FOR PCE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2pce
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/pcengine
)

target_include_directories(usbretro_ngc PUBLIC # FOR NGC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2gc
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/gamecube
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/include
)

target_include_directories(usbretro_nuon PUBLIC # FOR NUON
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2nuon
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/nuon
)

target_include_directories(usbretro_loopy PUBLIC # FOR LOOPY
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2loopy
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/loopy
)

target_include_directories(usbretro_3do PUBLIC # FOR 3DO
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb23do
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/3do
)

target_include_directories(usbretro_snes3do PUBLIC # FOR SNES23DO
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/snes23do
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/3do
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/snes
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/SNESpad/src
)

target_include_directories(usbretro_uart PUBLIC # FOR UART
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2uart
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/uart
)

target_include_directories(usbretro_usb PUBLIC # FOR USB
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
)

target_include_directories(usbretro_usb_rp2040zero PUBLIC # FOR USB on rp2040zero
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2usb
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
)

target_include_directories(usbretro_snes2usb PUBLIC # FOR SNES2USB
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/snes2usb
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/snes
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/SNESpad/src
)

target_include_directories(usbretro_controller_fisherprice PUBLIC # Controller: Fisher Price
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/controller
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
    ${CMAKE_CURRENT_SOURCE_DIR}/pad
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/uart
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/uart
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host
)

target_include_directories(usbretro_controller_fisherprice_analog PUBLIC # Controller: Fisher Price Analog
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/controller
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
    ${CMAKE_CURRENT_SOURCE_DIR}/pad
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/uart
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/uart
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host
)

target_include_directories(usbretro_controller_alpakka PUBLIC # Controller: Alpakka
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/controller
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
    ${CMAKE_CURRENT_SOURCE_DIR}/pad
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/uart
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/uart
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host
)

target_include_directories(usbretro_controller_macropad PUBLIC # Controller: MacroPad
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/controller
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbd
    ${CMAKE_CURRENT_SOURCE_DIR}/pad
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/uart
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host/uart
    ${CMAKE_CURRENT_SOURCE_DIR}/native/host
)

## SET TARGET LINK LIBS
set(COMMON_LIBRARIES
    pico_stdlib
    pico_multicore
    hardware_pio
    tinyusb_host
    tinyusb_board
    xinput_host
)

target_link_libraries(usbretro_pce PRIVATE # FOR PCE
    ${COMMON_LIBRARIES}
)

target_link_libraries(usbretro_ngc PRIVATE # FOR NGC
    ${COMMON_LIBRARIES}
    hardware_flash
)

target_link_libraries(usbretro_nuon PRIVATE # FOR NUON
    ${COMMON_LIBRARIES}
    pico_bit_ops
)

target_link_libraries(usbretro_loopy PRIVATE # FOR LOOPY
    ${COMMON_LIBRARIES}
    pico_bit_ops
)

target_link_libraries(usbretro_3do PRIVATE # FOR 3DO
    ${COMMON_LIBRARIES}
    hardware_dma
)

target_link_libraries(usbretro_snes3do PRIVATE # FOR SNES23DO
    ${COMMON_LIBRARIES}
    hardware_dma
)

target_link_libraries(usbretro_uart PRIVATE # FOR UART
    ${COMMON_LIBRARIES}
    hardware_uart
)

target_link_libraries(usbretro_usb PRIVATE # FOR USB (dual-role: host on PIO, device on native)
    ${COMMON_LIBRARIES}
    tinyusb_device
    tinyusb_pico_pio_usb
)

target_link_libraries(usbretro_usb_rp2040zero PRIVATE # FOR USB on rp2040zero (PIO host, native device)
    ${COMMON_LIBRARIES}
    tinyusb_device
    tinyusb_pico_pio_usb
)

target_link_libraries(usbretro_snes2usb PRIVATE # FOR SNES2USB (SNES input, USB device output - no host)
    pico_stdlib
    pico_multicore
    hardware_pio
    tinyusb_device
    tinyusb_board
)

target_link_libraries(usbretro_controller_fisherprice PRIVATE # Controller: Fisher Price
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_adc
    hardware_i2c
    hardware_pwm
    hardware_spi
    tinyusb_device
    tinyusb_board
)

target_link_libraries(usbretro_controller_fisherprice_analog PRIVATE # Controller: Fisher Price Analog
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_adc
    hardware_i2c
    hardware_pwm
    hardware_spi
    tinyusb_device
    tinyusb_board
)

target_link_libraries(usbretro_controller_alpakka PRIVATE # Controller: Alpakka
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_adc
    hardware_i2c
    hardware_pwm
    hardware_spi
    tinyusb_device
    tinyusb_board
)

target_link_libraries(usbretro_controller_macropad PRIVATE # Controller: MacroPad
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_adc
    hardware_i2c
    hardware_pwm
    hardware_spi
    tinyusb_device
    tinyusb_board
)
