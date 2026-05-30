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
    # src/devices/usb/hid/sony/ds5.c        (Phase 4)
    # src/devices/usb/hid/nintendo/switch_pro.c
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
endif()
