# libjoypad - Universal C library for USB and Bluetooth game controllers.
#
# Buildable standalone (host C compiler, no firmware toolchain required).
# Consumers: joypad-os firmware (RP2040/ESP32/nRF), joypad-web (WASM),
#            joypad-unreal, joypad-unity, plus the examples in this tree.
#
# Boundary contract: libjoypad sources may only include <stdint.h>,
# <stdbool.h>, <string.h>, <stddef.h> from libc and headers under
# include/joypad/. The standalone build has no firmware include paths
# available, so a stray "pico/stdlib.h" or "tusb.h" fails to compile.
#
# See .dev/docs/libjoypad.md for architecture.

cmake_minimum_required(VERSION 3.13)

project(libjoypad
    VERSION 0.1.0
    DESCRIPTION "Universal C library for USB and Bluetooth game controllers"
    LANGUAGES C
)

# ----------------------------------------------------------------------------
# Source list — empty for now (Phase 2 ships contract headers only).
# Phase 4 will add the first parser (devices/usb/hid/sony/ds5.c) and switch
# this from INTERFACE to OBJECT.
# ----------------------------------------------------------------------------

set(LIBJOYPAD_SOURCES
    src/devices/usb/hid/sony/ds3.c
    src/devices/usb/hid/sony/ds4.c
    src/devices/usb/hid/sony/ds5.c
    src/devices/usb/hid/sony/psc.c
    src/devices/usb/hid/nintendo/switch_pro.c
    # src/devices/bt/hid/sony/ds5_bt.c
    # src/registry.c
    # src/sinput.c
)

if(LIBJOYPAD_SOURCES)
    add_library(libjoypad OBJECT ${LIBJOYPAD_SOURCES})
else()
    # Header-only for now. Promote to OBJECT when the first source lands.
    add_library(libjoypad INTERFACE)
    set(LIBJOYPAD_INTERFACE TRUE)
endif()

# ----------------------------------------------------------------------------
# Public headers (the contract)
# ----------------------------------------------------------------------------

if(LIBJOYPAD_INTERFACE)
    target_include_directories(libjoypad INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_features(libjoypad INTERFACE c_std_11)
else()
    target_include_directories(libjoypad PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_features(libjoypad PUBLIC c_std_11)

    # Strict warnings for libjoypad's own translation units — boundary surfaces
    # any sloppy includes or platform leakage at build time.
    target_compile_options(libjoypad PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter   # legacy migration helpers have unused params
    )
endif()

# ----------------------------------------------------------------------------
# Tests / boundary check
# ----------------------------------------------------------------------------

option(LIBJOYPAD_BUILD_TESTS "Build libjoypad header-boundary + parser tests" ON)

if(LIBJOYPAD_BUILD_TESTS)
    enable_testing()

    # Boundary smoke test — compiles a tiny .c using ALL libjoypad headers
    # with -Wall -Wextra -Wpedantic. Proves the contract is self-contained
    # and survives strict warnings.
    add_executable(libjoypad_smoke tests/replay/smoke.c)
    target_link_libraries(libjoypad_smoke PRIVATE libjoypad)
    target_compile_options(libjoypad_smoke PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter   # legacy migration helpers
    )
    add_test(NAME libjoypad_smoke COMMAND libjoypad_smoke)

    # Per-driver tests
    add_executable(libjoypad_ds5_input tests/replay/ds5_input.c)
    target_link_libraries(libjoypad_ds5_input PRIVATE libjoypad)
    target_compile_options(libjoypad_ds5_input PRIVATE
        -Wall -Wextra -Wpedantic
    )
    add_test(NAME libjoypad_ds5_input COMMAND libjoypad_ds5_input)

    add_executable(libjoypad_switch_pro_input tests/replay/switch_pro_input.c)
    target_link_libraries(libjoypad_switch_pro_input PRIVATE libjoypad)
    target_compile_options(libjoypad_switch_pro_input PRIVATE
        -Wall -Wextra -Wpedantic
    )
    add_test(NAME libjoypad_switch_pro_input COMMAND libjoypad_switch_pro_input)

    add_executable(libjoypad_psc_input tests/replay/psc_input.c)
    target_link_libraries(libjoypad_psc_input PRIVATE libjoypad)
    target_compile_options(libjoypad_psc_input PRIVATE
        -Wall -Wextra -Wpedantic
    )
    add_test(NAME libjoypad_psc_input COMMAND libjoypad_psc_input)

    add_executable(libjoypad_ds4_input tests/replay/ds4_input.c)
    target_link_libraries(libjoypad_ds4_input PRIVATE libjoypad)
    target_compile_options(libjoypad_ds4_input PRIVATE
        -Wall -Wextra -Wpedantic
    )
    add_test(NAME libjoypad_ds4_input COMMAND libjoypad_ds4_input)

    add_executable(libjoypad_ds3_input tests/replay/ds3_input.c)
    target_link_libraries(libjoypad_ds3_input PRIVATE libjoypad)
    target_compile_options(libjoypad_ds3_input PRIVATE
        -Wall -Wextra -Wpedantic
    )
    add_test(NAME libjoypad_ds3_input COMMAND libjoypad_ds3_input)

    # Fixture replay — runs against tests/fixtures/<vendor>/<device>/captured/
    # when real-hardware captures are present; otherwise the runner exits OK
    # with a notice so it stays a green CI step until a capture session lands.
    add_executable(libjoypad_ds5_fixture_replay tests/replay/ds5_fixture_replay.c)
    target_link_libraries(libjoypad_ds5_fixture_replay PRIVATE libjoypad)
    target_compile_options(libjoypad_ds5_fixture_replay PRIVATE
        -Wall -Wextra -Wpedantic
    )
    add_test(
        NAME libjoypad_ds5_fixture_replay
        COMMAND libjoypad_ds5_fixture_replay
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
