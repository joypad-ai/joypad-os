# Zephyr app CMakeLists for Joypad on nRF52840 boards
#
# Supported boards: xiao_ble (Seeed XIAO), adafruit_feather_nrf52840
# Supported apps:   bt2usb (default), usb2usb (with MAX3421E FeatherWing),
#                   btusb2usb (MAX3421E + Bluetooth combined)
#
# Compiles shared sources from ../src/ alongside nRF-specific code.
# bt2usb:    BTstack from pico-sdk submodule (Zephyr HCI transport), TinyUSB device-only (nRF5x DCD).
# usb2usb:   TinyUSB device (nRF5x DCD) + TinyUSB host (MAX3421E SPI), no Bluetooth.
# btusb2usb: TinyUSB device + TinyUSB host (MAX3421E) + BTstack BLE host.

cmake_minimum_required(VERSION 3.20)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# APP_TYPE selects which app to build (default: bt2usb)
# Zephyr sysbuild doesn't forward custom -D variables to the inner project,
# so we detect app type from EXTRA_CONF_FILE (which IS forwarded via sysbuild cache).
if(DEFINED EXTRA_CONF_FILE AND EXTRA_CONF_FILE MATCHES "prj_controller_btusb")
    set(APP_TYPE "controller_btusb")
elseif(DEFINED EXTRA_CONF_FILE AND EXTRA_CONF_FILE MATCHES "prj_btusb2usb")
    set(APP_TYPE "btusb2usb")
elseif(DEFINED EXTRA_CONF_FILE AND EXTRA_CONF_FILE MATCHES "prj_usb2usb")
    set(APP_TYPE "usb2usb")
else()
    set(APP_TYPE "bt2usb")
endif()
message(STATUS "APP_TYPE: ${APP_TYPE}")

project(joypad_${APP_TYPE})

set(SHARED_SRC "${CMAKE_CURRENT_LIST_DIR}/../src")
set(BTSTACK_PATH "${SHARED_SRC}/lib/pico-sdk/lib/btstack")
set(TINYUSB_PATH "${SHARED_SRC}/lib/tinyusb")

# ============================================================================
# Common source files (shared by all app types)
# ============================================================================

set(SRCS
    # --- nRF-specific ---
    "src/main.c"
    "src/flash_nrf.c"
    "src/ws2812_nrf.c"
    "src/button_nrf.c"
    "${SHARED_SRC}/platform/nrf/platform_nrf.c"

    # --- Display (SSD1306 I2C on XIAO Expansion Board / FeatherWing OLED) ---
    "src/display_i2c_nrf.c"
    "${SHARED_SRC}/core/services/display/display.c"
    "${SHARED_SRC}/core/services/display/joy_anim.c"
    "${SHARED_SRC}/core/services/display/eyes_anim.c"

    # --- Core services ---
    "${SHARED_SRC}/core/app_registry.c"
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

    # --- HID parser (shared with USB host and BT paths) ---
    "${SHARED_SRC}/usb/usbh/hid/devices/generic/hid_parser.c"

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
    "${SHARED_SRC}/usb/usbd"
    "${SHARED_SRC}/lib/libxsm3"
    # TinyUSB
    "${TINYUSB_PATH}/src"
)

# ============================================================================
# App-specific sources and definitions
# ============================================================================

if(APP_TYPE STREQUAL "usb2usb")
    # -----------------------------------------------------------------------
    # usb2usb: USB Host (MAX3421E) → USB Device (nRF native)
    # No Bluetooth — pure USB adapter
    # -----------------------------------------------------------------------
    list(APPEND SRCS
        # --- App ---
        "${SHARED_SRC}/apps/usb2usb/app.c"

        # --- MAX3421E host (nRF Zephyr port) ---
        "src/max3421_host_nrf.c"

        # --- USB Host layer ---
        "${SHARED_SRC}/usb/usbh/usbh.c"
        "${SHARED_SRC}/usb/usbh/hid/hid.c"
        "${SHARED_SRC}/usb/usbh/hid/hid_registry.c"
        "${SHARED_SRC}/usb/usbh/hid/hid_utils.c"
        "${SHARED_SRC}/usb/usbh/xinput/xinput.c"
        "${SHARED_SRC}/usb/usbh/xbone_auth/xbone_auth.c"

        # --- USB HID device drivers ---
        "${SHARED_SRC}/usb/usbh/hid/devices/generic/hid_gamepad.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/generic/hid_keyboard.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/generic/hid_mouse.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/generic/sinput_host.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/8bitdo/8bitdo_bta.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/8bitdo/8bitdo_m30.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/8bitdo/8bitdo_neo.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/8bitdo/8bitdo_pce.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/dragonrise/dragonrise.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/google/google_stadia.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/hori/hori_horipad.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/hori/hori_pokken.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/logitech/logitech_wingman.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/microsoft/ms_sidewinder_commander.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/microsoft/ms_sidewinder_dualstrike.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/misc/triple_adapter_v1.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/misc/triple_adapter_v2.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/nintendo/gamecube_adapter.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/nintendo/switch2_pro.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/nintendo/switch_pro.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/raphnet/raphnet_pce.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/sega/sega_astrocity.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/sony/sony_ds3.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/sony/sony_ds4.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/sony/sony_ds5.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/sony/sony_psc.c"

        # --- XInput host driver (tusb_xinput library) ---
        "${SHARED_SRC}/lib/tusb_xinput/xinput_host.c"

        # --- TinyUSB host stack ---
        "${TINYUSB_PATH}/src/host/usbh.c"
        "${TINYUSB_PATH}/src/host/hub.c"
        "${TINYUSB_PATH}/src/class/hid/hid_host.c"
        "${TINYUSB_PATH}/src/class/vendor/vendor_host.c"
        "${TINYUSB_PATH}/src/portable/analog/max3421/hcd_max3421.c"
    )

    list(APPEND INCLUDE_DIRS
        "${SHARED_SRC}/apps/usb2usb"
        "${SHARED_SRC}/usb/usbh"
        "${SHARED_SRC}/lib/tusb_xinput"
    )

    target_compile_definitions(app PRIVATE
        CONFIG_USB2USB=1
        CONFIG_USB=1
        CONFIG_MAX3421=1
        PLATFORM_NRF=1
        CONFIG_NO_NEOPIXEL=1
        DISABLE_DISPLAY_SPI=1
        OLED_I2C_DISPLAY=1
        CFG_TUSB_CONFIG_FILE="tusb_config_nrf.h"
    )

elseif(APP_TYPE STREQUAL "btusb2usb")
    # -----------------------------------------------------------------------
    # btusb2usb: USB Host (MAX3421E) + Bluetooth → USB Device (nRF native)
    # Combines usb2usb and bt2usb sources into one firmware
    # -----------------------------------------------------------------------
    list(APPEND SRCS
        # --- App (shared with usb2usb) ---
        "${SHARED_SRC}/apps/usb2usb/app.c"

        # --- MAX3421E host (nRF Zephyr port) ---
        "src/max3421_host_nrf.c"

        # --- USB Host layer ---
        "${SHARED_SRC}/usb/usbh/usbh.c"
        "${SHARED_SRC}/usb/usbh/hid/hid.c"
        "${SHARED_SRC}/usb/usbh/hid/hid_registry.c"
        "${SHARED_SRC}/usb/usbh/hid/hid_utils.c"
        "${SHARED_SRC}/usb/usbh/xinput/xinput.c"
        "${SHARED_SRC}/usb/usbh/xbone_auth/xbone_auth.c"

        # --- USB HID device drivers ---
        "${SHARED_SRC}/usb/usbh/hid/devices/generic/hid_gamepad.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/generic/hid_keyboard.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/generic/hid_mouse.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/generic/sinput_host.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/8bitdo/8bitdo_bta.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/8bitdo/8bitdo_m30.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/8bitdo/8bitdo_neo.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/8bitdo/8bitdo_pce.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/dragonrise/dragonrise.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/google/google_stadia.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/hori/hori_horipad.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/hori/hori_pokken.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/logitech/logitech_wingman.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/microsoft/ms_sidewinder_commander.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/microsoft/ms_sidewinder_dualstrike.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/misc/triple_adapter_v1.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/misc/triple_adapter_v2.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/nintendo/gamecube_adapter.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/nintendo/switch2_pro.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/nintendo/switch_pro.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/raphnet/raphnet_pce.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/sega/sega_astrocity.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/sony/sony_ds3.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/sony/sony_ds4.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/sony/sony_ds5.c"
        "${SHARED_SRC}/usb/usbh/hid/devices/vendors/sony/sony_psc.c"

        # --- XInput host driver (tusb_xinput library) ---
        "${SHARED_SRC}/lib/tusb_xinput/xinput_host.c"

        # --- TinyUSB host stack ---
        "${TINYUSB_PATH}/src/host/usbh.c"
        "${TINYUSB_PATH}/src/host/hub.c"
        "${TINYUSB_PATH}/src/class/hid/hid_host.c"
        "${TINYUSB_PATH}/src/class/vendor/vendor_host.c"
        "${TINYUSB_PATH}/src/portable/analog/max3421/hcd_max3421.c"

        # --- nRF BTstack TLV ---
        "src/btstack_tlv_nrf.c"

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

        # --- BTstack Classic (compiles but dead code on BLE-only boards) ---
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
    )

    list(APPEND INCLUDE_DIRS
        "${SHARED_SRC}/apps/usb2usb"
        "${SHARED_SRC}/usb/usbh"
        "${SHARED_SRC}/lib/tusb_xinput"
        "${SHARED_SRC}/bt"
        "${SHARED_SRC}/bt/btstack"
        "${SHARED_SRC}/bt/bthid"
        # BTstack includes
        "${BTSTACK_PATH}/src"
        "${BTSTACK_PATH}/src/ble"
        "${BTSTACK_PATH}/platform/embedded"
        "${BTSTACK_PATH}/3rd-party/micro-ecc"
        "${BTSTACK_PATH}/3rd-party/rijndael"
        "${BTSTACK_PATH}/chipset/zephyr"
    )

    target_compile_definitions(app PRIVATE
        CONFIG_BTUSB2USB=1
        CONFIG_USB=1
        CONFIG_MAX3421=1
        BTSTACK_USE_NRF=1
        ENABLE_BTSTACK=1
        PLATFORM_NRF=1
        CONFIG_NO_NEOPIXEL=1
        DISABLE_DISPLAY_SPI=1
        OLED_I2C_DISPLAY=1
        CFG_TUSB_CONFIG_FILE="tusb_config_nrf.h"
    )

elseif(APP_TYPE STREQUAL "controller_btusb")
    # -----------------------------------------------------------------------
    # controller_btusb: Sensor inputs → BLE Peripheral + USB Device
    # BTstack BLE peripheral (HOGP), no USB host, no btstack_host/bthid
    # -----------------------------------------------------------------------
    list(APPEND SRCS
        # --- App ---
        "src/stubs_peripheral.c"
        "${SHARED_SRC}/apps/controller_btusb/app.c"

        # --- Pad input + config ---
        "${SHARED_SRC}/pad/pad_input.c"
        "${SHARED_SRC}/pad/pad_config_flash.c"
        "src/pad_config_storage_nrf.c"
        "src/platform_gpio_nrf.c"

        # --- Sensor drivers ---
        "${SHARED_SRC}/drivers/seesaw/seesaw.c"
        "${SHARED_SRC}/drivers/joywing/joywing_input.c"
        "${SHARED_SRC}/platform/nrf/platform_i2c_nrf.c"

        # --- BLE output (peripheral mode) ---
        "${SHARED_SRC}/bt/ble_output/ble_output.c"
        "${SHARED_SRC}/bt/ble_output/ble_output_keyboard.c"
        "${SHARED_SRC}/bt/ble_output/ble_output_mouse.c"
        "${SHARED_SRC}/bt/ble_output/ble_output_xbox.c"
        "${SHARED_SRC}/bt/ble_output/ble_nus.c"
        "${SHARED_SRC}/bt/ble_output/ble_xbox_gatt_db.c"

        # --- BT transport ---
        "${SHARED_SRC}/bt/transport/bt_transport.c"
        "${SHARED_SRC}/bt/transport/bt_transport_nrf.c"

        # --- BT Central (host mode — scan for BLE controllers) ---
        "${SHARED_SRC}/bt/btstack/btstack_host.c"
        "${SHARED_SRC}/bt/btstack/bt_device_db.c"
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

        # --- nRF BTstack TLV ---
        "src/btstack_tlv_nrf.c"

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

        # --- BTstack GATT server (peripheral) + client services (central) ---
        "${BTSTACK_PATH}/src/ble/att_server.c"
        "${BTSTACK_PATH}/src/ble/gatt-service/hids_client.c"
        "${BTSTACK_PATH}/src/ble/gatt-service/battery_service_client.c"
        "${BTSTACK_PATH}/src/ble/gatt-service/device_information_service_client.c"
        "${BTSTACK_PATH}/src/ble/gatt-service/hids_device.c"
        "${BTSTACK_PATH}/src/ble/gatt-service/battery_service_server.c"
        "${BTSTACK_PATH}/src/ble/gatt-service/device_information_service_server.c"
        "${BTSTACK_PATH}/src/ble/gatt-service/nordic_spp_service_server.c"
        "${BTSTACK_PATH}/src/btstack_hid_parser.c"

        # --- BTstack Classic (compiles but dead code on BLE-only boards) ---
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
    )

    list(APPEND INCLUDE_DIRS
        "${SHARED_SRC}/apps/controller_btusb"
        "${SHARED_SRC}/pad"
        "${SHARED_SRC}/drivers"
        "${SHARED_SRC}/bt"
        "${SHARED_SRC}/bt/btstack"
        "${SHARED_SRC}/bt/bthid"
        "${SHARED_SRC}/bt/ble_output"
        # BTstack includes
        "${BTSTACK_PATH}/src"
        "${BTSTACK_PATH}/src/ble"
        "${BTSTACK_PATH}/platform/embedded"
        "${BTSTACK_PATH}/3rd-party/micro-ecc"
        "${BTSTACK_PATH}/3rd-party/rijndael"
        "${BTSTACK_PATH}/chipset/zephyr"
    )

    target_compile_definitions(app PRIVATE
        CONFIG_CONTROLLER_BTUSB=1
        CONFIG_USB=1
        CONFIG_PAD_INPUT=1
        SENSOR_PAD=1
        WS2812_PIN=16
        WS2812_NUM_PIXELS=1
        DISABLE_USB_HOST=1
        BTSTACK_USE_NRF=1
        ENABLE_BTSTACK=1
        REQUIRE_BLE_OUTPUT=1
        REQUIRE_BT_INPUT=1
        SENSOR_JOYWING=1
        PLATFORM_NRF=1
        DISABLE_DISPLAY_SPI=1
        OLED_I2C_DISPLAY=1
        CFG_TUSB_CONFIG_FILE="tusb_config_nrf.h"
    )

else()
    # -----------------------------------------------------------------------
    # bt2usb (default): Bluetooth → USB Device
    # BTstack BLE host, no USB host input
    # -----------------------------------------------------------------------
    list(APPEND SRCS
        # --- nRF BTstack TLV ---
        "src/btstack_tlv_nrf.c"

        # --- App ---
        "${SHARED_SRC}/apps/bt2usb/app.c"

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

        # --- BTstack Classic (compiles but dead code on BLE-only boards) ---
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
    )

    list(APPEND INCLUDE_DIRS
        "${SHARED_SRC}/apps/bt2usb"
        "${SHARED_SRC}/bt"
        "${SHARED_SRC}/bt/btstack"
        "${SHARED_SRC}/bt/bthid"
        # BTstack includes
        "${BTSTACK_PATH}/src"
        "${BTSTACK_PATH}/src/ble"
        "${BTSTACK_PATH}/platform/embedded"
        "${BTSTACK_PATH}/3rd-party/micro-ecc"
        "${BTSTACK_PATH}/3rd-party/rijndael"
        "${BTSTACK_PATH}/chipset/zephyr"
    )

    target_compile_definitions(app PRIVATE
        CONFIG_BT2USB=1
        CONFIG_USB=1
        DISABLE_USB_HOST=1
        REQUIRE_BT_INPUT=1
        BTSTACK_USE_NRF=1
        ENABLE_BTSTACK=1
        PLATFORM_NRF=1
        CONFIG_NO_NEOPIXEL=1
        DISABLE_DISPLAY_SPI=1
        OLED_I2C_DISPLAY=1
        CFG_TUSB_CONFIG_FILE="tusb_config_nrf.h"
    )

endif()

# ============================================================================
# Register sources with Zephyr build
# ============================================================================

target_sources(app PRIVATE ${SRCS})
target_include_directories(app PRIVATE ${INCLUDE_DIRS})

# ============================================================================
# GATT header generation (peripheral mode — controller_btusb)
# ============================================================================

if(APP_TYPE STREQUAL "controller_btusb")
    set(COMPILE_GATT "${BTSTACK_PATH}/tool/compile_gatt.py")

    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ble_gamepad.h"
        COMMAND python3 ${COMPILE_GATT}
            "${SHARED_SRC}/bt/ble_output/ble_gamepad.gatt"
            "${CMAKE_CURRENT_BINARY_DIR}/ble_gamepad.h"
        DEPENDS "${SHARED_SRC}/bt/ble_output/ble_gamepad.gatt"
        COMMENT "Generating ble_gamepad.h from GATT definition"
    )

    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ble_xbox.h"
        COMMAND python3 ${COMPILE_GATT}
            "${SHARED_SRC}/bt/ble_output/ble_xbox.gatt"
            "${CMAKE_CURRENT_BINARY_DIR}/ble_xbox.h"
        DEPENDS "${SHARED_SRC}/bt/ble_output/ble_xbox.gatt"
        COMMENT "Generating ble_xbox.h from GATT definition"
    )

    add_custom_target(gatt_headers DEPENDS
        "${CMAKE_CURRENT_BINARY_DIR}/ble_gamepad.h"
        "${CMAKE_CURRENT_BINARY_DIR}/ble_xbox.h"
    )
    add_dependencies(app gatt_headers)

    # Include build dir for generated GATT headers
    target_include_directories(app PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()

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

# Board-specific compile definitions
if(BOARD MATCHES "adafruit_feather_nrf52840")
    set(BOARD_DEFINE "BOARD_FEATHER_NRF52840=1")
    set(BOARD_NAME_STR "adafruit_feather_nrf52840")
else()
    set(BOARD_NAME_STR "xiao_ble")
endif()

target_compile_definitions(app PRIVATE
    JOYPAD_VERSION="${JOYPAD_VERSION}"
    GIT_COMMIT="${GIT_COMMIT}"
    BUILD_TIME="${BUILD_TIME}"
    BOARD_NAME="${BOARD_NAME_STR}"
    ${BOARD_DEFINE}
)

# Disable -Werror for cross-compiled shared code
target_compile_options(app PRIVATE -Wno-error)

# Force-include btstack_config.h so all BTstack headers see ENABLE_BLE etc.
# BTstack headers check #ifdef ENABLE_BLE but don't include btstack_config.h themselves.
if(APP_TYPE STREQUAL "controller_btusb" OR APP_TYPE STREQUAL "bt2usb" OR APP_TYPE STREQUAL "btusb2usb")
    target_compile_options(app PRIVATE -include btstack_config.h)
endif()
