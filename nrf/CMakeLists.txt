# Zephyr app CMakeLists for Joypad bt2usb on Seeed XIAO nRF52840 (xiao_ble)
#
# Compiles shared sources from ../src/ alongside nRF-specific code.
# BTstack from pico-sdk submodule (Zephyr HCI transport), TinyUSB device-only (nRF5x DCD).

cmake_minimum_required(VERSION 3.20)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(joypad_bt2usb)

set(SHARED_SRC "${CMAKE_CURRENT_LIST_DIR}/../src")
set(BTSTACK_PATH "${SHARED_SRC}/lib/pico-sdk/lib/btstack")
set(TINYUSB_PATH "${SHARED_SRC}/lib/tinyusb")

# ============================================================================
# Source files
# ============================================================================

set(SRCS
    # --- nRF-specific ---
    "src/main.c"
    "src/flash_nrf.c"
    "src/ws2812_nrf.c"
    "src/button_nrf.c"
    "src/btstack_tlv_nrf.c"
    "${SHARED_SRC}/platform/nrf/platform_nrf.c"

    # --- App ---
    "${SHARED_SRC}/apps/bt2usb/app.c"

    # --- Display (SSD1306 I2C on XIAO Expansion Board) ---
    "src/display_i2c_nrf.c"
    "${SHARED_SRC}/core/services/display/display.c"

    # --- Core services ---
    "${SHARED_SRC}/core/router/router.c"
    "${SHARED_SRC}/core/services/leds/leds.c"
    "${SHARED_SRC}/core/services/storage/storage.c"
    "${SHARED_SRC}/core/services/codes/codes.c"
    "${SHARED_SRC}/core/services/hotkeys/hotkeys.c"
    "${SHARED_SRC}/core/services/players/manager.c"
    "${SHARED_SRC}/core/services/players/feedback.c"
    "${SHARED_SRC}/core/services/profiles/profile.c"
    "${SHARED_SRC}/core/services/profiles/profile_indicator.c"

    # --- USB Device ---
    "${SHARED_SRC}/usb/usbd/usbd.c"
    "${SHARED_SRC}/usb/usbd/modes/hid_mode.c"
    "${SHARED_SRC}/usb/usbd/modes/sinput_mode.c"
    "${SHARED_SRC}/usb/usbd/modes/xinput_mode.c"
    "${SHARED_SRC}/usb/usbd/modes/switch_mode.c"
    "${SHARED_SRC}/usb/usbd/modes/ps3_mode.c"
    "${SHARED_SRC}/usb/usbd/modes/psclassic_mode.c"
    "${SHARED_SRC}/usb/usbd/modes/pcemini_mode.c"
    "${SHARED_SRC}/usb/usbd/modes/ps4_mode.c"
    "${SHARED_SRC}/usb/usbd/modes/xid_mode.c"
    "${SHARED_SRC}/usb/usbd/modes/xbone_mode.c"
    "${SHARED_SRC}/usb/usbd/modes/xac_mode.c"
    "${SHARED_SRC}/usb/usbd/modes/kbmouse_mode.c"
    "${SHARED_SRC}/usb/usbd/modes/gc_adapter_mode.c"
    "${SHARED_SRC}/usb/usbd/cdc/cdc.c"
    "${SHARED_SRC}/usb/usbd/cdc/cdc_protocol.c"
    "${SHARED_SRC}/usb/usbd/cdc/cdc_commands.c"
    "${SHARED_SRC}/usb/usbd/drivers/tud_xid.c"
    "${SHARED_SRC}/usb/usbd/drivers/tud_xinput.c"
    "${SHARED_SRC}/usb/usbd/drivers/tud_xbone.c"
    "${SHARED_SRC}/usb/usbd/drivers/xgip_protocol.c"
    "${SHARED_SRC}/usb/usbd/kbmouse/kbmouse.c"
    "${SHARED_SRC}/lib/libxsm3/xsm3.c"
    "${SHARED_SRC}/lib/libxsm3/usbdsec.c"
    "${SHARED_SRC}/lib/libxsm3/excrypt_des.c"
    "${SHARED_SRC}/lib/libxsm3/excrypt_parve.c"
    "${SHARED_SRC}/lib/libxsm3/excrypt_sha.c"

    # --- HID parser (shared with USB path) ---
    "${SHARED_SRC}/usb/usbh/hid/devices/generic/hid_parser.c"

    # --- BT HID device drivers ---
    "${SHARED_SRC}/bt/bthid/bthid.c"
    "${SHARED_SRC}/bt/bthid/bthid_registry.c"
    "${SHARED_SRC}/bt/bthid/devices/generic/bthid_gamepad.c"
    "${SHARED_SRC}/bt/bthid/devices/vendors/sony/ds3_bt.c"
    "${SHARED_SRC}/bt/bthid/devices/vendors/sony/ds4_bt.c"
    "${SHARED_SRC}/bt/bthid/devices/vendors/sony/ds5_bt.c"
    "${SHARED_SRC}/bt/bthid/devices/vendors/nintendo/switch_pro_bt.c"
    "${SHARED_SRC}/bt/bthid/devices/vendors/nintendo/switch2_ble.c"
    "${SHARED_SRC}/bt/bthid/devices/vendors/nintendo/wii_u_pro_bt.c"
    "${SHARED_SRC}/bt/bthid/devices/vendors/nintendo/wiimote_bt.c"
    "${SHARED_SRC}/bt/bthid/devices/vendors/microsoft/xbox_bt.c"
    "${SHARED_SRC}/bt/bthid/devices/vendors/microsoft/xbox_ble.c"
    "${SHARED_SRC}/bt/bthid/devices/vendors/google/stadia_bt.c"

    # --- BTstack host + transport ---
    "${SHARED_SRC}/bt/btstack/btstack_host.c"
    "${SHARED_SRC}/bt/btstack/bt_device_db.c"
    "${SHARED_SRC}/bt/transport/bt_transport.c"
    "${SHARED_SRC}/bt/transport/bt_transport_nrf.c"

    # --- BTstack core ---
    "${BTSTACK_PATH}/src/btstack_linked_list.c"
    "${BTSTACK_PATH}/src/btstack_memory.c"
    "${BTSTACK_PATH}/src/btstack_memory_pool.c"
    "${BTSTACK_PATH}/src/btstack_run_loop.c"
    "${BTSTACK_PATH}/src/btstack_run_loop_base.c"
    "${BTSTACK_PATH}/src/btstack_util.c"
    "${BTSTACK_PATH}/src/btstack_tlv.c"
    "${BTSTACK_PATH}/src/btstack_tlv_none.c"
    "${BTSTACK_PATH}/src/btstack_crypto.c"
    "${BTSTACK_PATH}/src/btstack_ring_buffer.c"
    "${BTSTACK_PATH}/src/hci.c"
    "${BTSTACK_PATH}/src/hci_cmd.c"
    "${BTSTACK_PATH}/src/hci_dump.c"
    "${BTSTACK_PATH}/src/hci_event.c"
    "${BTSTACK_PATH}/src/hci_event_builder.c"
    "${BTSTACK_PATH}/src/l2cap.c"
    "${BTSTACK_PATH}/src/l2cap_signaling.c"
    "${BTSTACK_PATH}/src/ad_parser.c"

    # --- BTstack BLE ---
    "${BTSTACK_PATH}/src/ble/sm.c"
    "${BTSTACK_PATH}/src/ble/att_dispatch.c"
    "${BTSTACK_PATH}/src/ble/att_db.c"
    "${BTSTACK_PATH}/src/ble/gatt_client.c"
    "${BTSTACK_PATH}/src/ble/gatt_service_client.c"
    "${BTSTACK_PATH}/src/ble/le_device_db_memory.c"
    "${BTSTACK_PATH}/src/ble/le_device_db_tlv.c"
    "${BTSTACK_PATH}/src/ble/gatt-service/hids_client.c"
    "${BTSTACK_PATH}/src/ble/gatt-service/device_information_service_client.c"
    "${BTSTACK_PATH}/src/ble/gatt-service/battery_service_client.c"

    # --- BTstack Classic (compiles but dead code on XIAO nRF52840 BLE-only) ---
    "${BTSTACK_PATH}/src/classic/sdp_client.c"
    "${BTSTACK_PATH}/src/classic/sdp_server.c"
    "${BTSTACK_PATH}/src/classic/sdp_util.c"
    "${BTSTACK_PATH}/src/classic/device_id_server.c"
    "${BTSTACK_PATH}/src/classic/hid_host.c"
    "${BTSTACK_PATH}/src/classic/btstack_link_key_db_memory.c"
    "${BTSTACK_PATH}/src/classic/btstack_link_key_db_tlv.c"

    # --- BTstack 3rd-party ---
    "${BTSTACK_PATH}/3rd-party/micro-ecc/uECC.c"
    "${BTSTACK_PATH}/3rd-party/rijndael/rijndael.c"

    # --- BTstack platform: embedded + Zephyr chipset ---
    "${BTSTACK_PATH}/platform/embedded/hci_dump_embedded_stdout.c"
    "${BTSTACK_PATH}/chipset/zephyr/btstack_chipset_zephyr.c"

    # --- TinyUSB device stack ---
    "${TINYUSB_PATH}/src/tusb.c"
    "${TINYUSB_PATH}/src/common/tusb_fifo.c"
    "${TINYUSB_PATH}/src/device/usbd.c"
    "${TINYUSB_PATH}/src/device/usbd_control.c"
    "${TINYUSB_PATH}/src/class/cdc/cdc_device.c"
    "${TINYUSB_PATH}/src/class/hid/hid_device.c"
    "${TINYUSB_PATH}/src/portable/nordic/nrf5x/dcd_nrf5x.c"
)

set(INCLUDE_DIRS
    "src"
    "${SHARED_SRC}"
    "${SHARED_SRC}/apps/bt2usb"
    "${SHARED_SRC}/usb/usbd"
    "${SHARED_SRC}/bt"
    "${SHARED_SRC}/bt/btstack"
    "${SHARED_SRC}/bt/bthid"
    "${SHARED_SRC}/lib/libxsm3"
    # BTstack includes
    "${BTSTACK_PATH}/src"
    "${BTSTACK_PATH}/src/ble"
    "${BTSTACK_PATH}/platform/embedded"
    "${BTSTACK_PATH}/3rd-party/micro-ecc"
    "${BTSTACK_PATH}/3rd-party/rijndael"
    "${BTSTACK_PATH}/chipset/zephyr"
    # TinyUSB
    "${TINYUSB_PATH}/src"
)

# ============================================================================
# Register sources with Zephyr build
# ============================================================================

target_sources(app PRIVATE ${SRCS})
target_include_directories(app PRIVATE ${INCLUDE_DIRS})

# ============================================================================
# Compile definitions
# ============================================================================

target_compile_definitions(app PRIVATE
    CONFIG_BT2USB=1
    CONFIG_USB=1
    DISABLE_USB_HOST=1
    BTSTACK_USE_NRF=1
    ENABLE_BTSTACK=1
    PLATFORM_NRF=1
    CONFIG_NO_NEOPIXEL=1
    DISABLE_DISPLAY_SPI=1
    OLED_I2C_DISPLAY=1
    # Use nRF-specific TinyUSB config instead of src/tusb_config.h
    CFG_TUSB_CONFIG_FILE="tusb_config_nrf.h"
)

# ============================================================================
# Version and build info
# ============================================================================

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../VERSION")
    file(READ "${CMAKE_CURRENT_LIST_DIR}/../VERSION" JOYPAD_VERSION)
    string(STRIP "${JOYPAD_VERSION}" JOYPAD_VERSION)
else()
    set(JOYPAD_VERSION "0.0.0")
endif()

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

string(TIMESTAMP BUILD_TIME "%Y-%m-%d %H:%M:%S")

target_compile_definitions(app PRIVATE
    JOYPAD_VERSION="${JOYPAD_VERSION}"
    GIT_COMMIT="${GIT_COMMIT}"
    BUILD_TIME="${BUILD_TIME}"
    BOARD_NAME="xiao_ble"
)

# Disable -Werror for cross-compiled shared code
target_compile_options(app PRIVATE -Wno-error)
