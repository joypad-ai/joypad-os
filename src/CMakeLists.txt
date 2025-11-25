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

add_executable(usbretro_pce) # For the usbretro_pce target
add_executable(usbretro_ngc) # For the usbretro_ngc target
add_executable(usbretro_xb1) # For the usbretro_xb1 target
add_executable(usbretro_nuon) # For the usbretro_nuon target
add_executable(usbretro_loopy) # For the usbretro_loopy target
add_executable(usbretro_3do) # For the usbretro_3do target

if (PICO_BOARD STREQUAL "pico")
    message(STATUS "PICO_BOARD is '${PICO_BOARD}', applying RPI_PICO_BUILD definition for usbretro_pce.")
    target_compile_definitions(usbretro_pce PRIVATE RPI_PICO_BUILD=1)
endif()

target_compile_definitions(usbretro_pce PRIVATE CONFIG_PCE=1)
target_compile_definitions(usbretro_ngc PRIVATE CONFIG_NGC=1)
target_compile_definitions(usbretro_xb1 PRIVATE CONFIG_XB1=1)
target_compile_definitions(usbretro_nuon PRIVATE CONFIG_NUON=1)
target_compile_definitions(usbretro_loopy PRIVATE CONFIG_LOOPY=1)
target_compile_definitions(usbretro_3do PRIVATE CONFIG_3DO=1)

target_compile_options(usbretro_pce PRIVATE -O3) # Set optimization level to O3 for your_target
target_compile_options(usbretro_ngc PRIVATE -O3) # Set optimization level to O3 for your_target
target_compile_options(usbretro_xb1 PRIVATE -O3) # Set optimization level to O3 for your_target
target_compile_options(usbretro_nuon PRIVATE -O3) # Set optimization level to O3 for your_target
target_compile_options(usbretro_loopy PRIVATE -O3) # Set optimization level to O3 for your_target
target_compile_options(usbretro_3do PRIVATE -O3) # Set optimization level to O3 for your_target

if(CMAKE_BUILD_TYPE MATCHES Debug)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Og")
else()
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
endif()

pico_add_extra_outputs(usbretro_pce)
pico_add_extra_outputs(usbretro_ngc)
pico_add_extra_outputs(usbretro_xb1)
pico_add_extra_outputs(usbretro_nuon)
pico_add_extra_outputs(usbretro_loopy)
pico_add_extra_outputs(usbretro_3do)

pico_generate_pio_header(usbretro_pce ${CMAKE_CURRENT_LIST_DIR}/native/device/pcengine/plex.pio )
pico_generate_pio_header(usbretro_pce ${CMAKE_CURRENT_LIST_DIR}/native/device/pcengine/clock.pio )
pico_generate_pio_header(usbretro_pce ${CMAKE_CURRENT_LIST_DIR}/native/device/pcengine/select.pio )
pico_generate_pio_header(usbretro_pce ${CMAKE_CURRENT_LIST_DIR}/core/services/leds/ws2812.pio )
pico_generate_pio_header(usbretro_ngc ${CMAKE_CURRENT_LIST_DIR}/lib/joybus-pio/src/joybus.pio )
pico_generate_pio_header(usbretro_ngc ${CMAKE_CURRENT_LIST_DIR}/core/services/leds/ws2812.pio )
pico_generate_pio_header(usbretro_xb1 ${CMAKE_CURRENT_LIST_DIR}/core/services/leds/ws2812.pio )
pico_generate_pio_header(usbretro_nuon ${CMAKE_CURRENT_LIST_DIR}/native/device/nuon/polyface_read.pio )
pico_generate_pio_header(usbretro_nuon ${CMAKE_CURRENT_LIST_DIR}/native/device/nuon/polyface_send.pio )
pico_generate_pio_header(usbretro_nuon ${CMAKE_CURRENT_LIST_DIR}/core/services/leds/ws2812.pio )
pico_generate_pio_header(usbretro_loopy ${CMAKE_CURRENT_LIST_DIR}/native/device/loopy/loopy.pio )
pico_generate_pio_header(usbretro_loopy ${CMAKE_CURRENT_LIST_DIR}/core/services/leds/ws2812.pio )
pico_generate_pio_header(usbretro_3do ${CMAKE_CURRENT_LIST_DIR}/native/device/3do/sampling.pio )
pico_generate_pio_header(usbretro_3do ${CMAKE_CURRENT_LIST_DIR}/native/device/3do/output.pio )
pico_generate_pio_header(usbretro_3do ${CMAKE_CURRENT_LIST_DIR}/core/services/leds/ws2812.pio )

## SET TARGE SOURCES
set(COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/leds/ws2812.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/players/feedback.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/storage/flash.c
    # output.c removed - apps now provide output interface via app_get_output_interface()

    # Core Services
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/hotkey/hotkey.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/players/manager.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/services/players/profile.c

    # Core Router System
    ${CMAKE_CURRENT_SOURCE_DIR}/core/router/router.c

    # USB Host - HID Protocol Stack
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/hid.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/hid_utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/hid_registry.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/generic/hid_keyboard.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/generic/hid_mouse.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/generic/hid_parser.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/generic/hid_gamepad.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/8bitdo/8bitdo_bta.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/8bitdo/8bitdo_neo.c
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
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/dragonrise/dragonrise.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/misc/triple_adapter_v1.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/hid/devices/vendors/misc/triple_adapter_v2.c

    # USB Host - X-input Protocol
    ${CMAKE_CURRENT_SOURCE_DIR}/usb/usbh/xinput/xinput.c

    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
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
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/gcusb/app.c
)

target_sources(usbretro_xb1 PUBLIC # For XB1
    ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/xboxone/xboxone_device.c
    # Phase 5: App layer
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/usb2xb1/app.c
)

target_sources(usbretro_nuon PUBLIC # For NUON
    ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/nuon/nuon_device.c
    # Phase 5: App layer
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/nuonusb/app.c
)

target_sources(usbretro_loopy PUBLIC # For (Casio) LOOPY
    ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/loopy/loopy_device.c
    # Phase 5: App layer
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/loopyusb/app.c
)

target_sources(usbretro_3do PUBLIC # For 3DO
    ${COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/3do/3do_device.c
    # Phase 5: App layer
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/3dousb/app.c
)

## SET TARGET INCLUDE DIRS
target_include_directories(usbretro_pce PUBLIC # FOR PCE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/pcengine
)

target_include_directories(usbretro_ngc PUBLIC # FOR NGC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/gamecube
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/joybus-pio/include
)

target_include_directories(usbretro_xb1 PUBLIC # FOR XB1
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/xboxone
)

target_include_directories(usbretro_nuon PUBLIC # FOR NUON
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/nuon
)

target_include_directories(usbretro_loopy PUBLIC # FOR LOOPY
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/loopy
)

target_include_directories(usbretro_3do PUBLIC # FOR 3DO
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/native/device/3do
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

target_link_libraries(usbretro_xb1 PRIVATE # FOR XB1
    ${COMMON_LIBRARIES}
    pico_i2c_slave
    hardware_i2c
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
