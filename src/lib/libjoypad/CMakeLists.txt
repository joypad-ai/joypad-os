# libjoypad - Universal C library for USB and Bluetooth game controllers
#
# Builds as an OBJECT library with no firmware include paths.
# Consumers: joypad-os firmware (RP2040/ESP32/nRF52), joypad-web (WASM),
#            joypad-unreal, joypad-unity, examples/minimal-c.
#
# See .dev/docs/libjoypad.md for architecture.

cmake_minimum_required(VERSION 3.13)

project(libjoypad C)

# Public headers under include/joypad/
add_library(libjoypad OBJECT
    # src/devices/ ... (populated as drivers are ported)
    # src/registry.c
    # src/sinput.c
)

target_include_directories(libjoypad PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Boundary enforcement: only the C standard library and libjoypad's own headers
# are available. No pico-sdk, no TinyUSB, no platform HAL.
target_compile_features(libjoypad PUBLIC c_std_11)

# Until at least one source file is added, this is a header-only object library.
set_target_properties(libjoypad PROPERTIES LINKER_LANGUAGE C)
