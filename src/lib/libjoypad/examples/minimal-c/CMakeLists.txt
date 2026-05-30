# examples/minimal-c — plain C + HIDAPI consumer of libjoypad.
#
# Build standalone:
#   cmake -B build
#   cmake --build build
#   ./build/joypad_minimal_c
#
# Requires libhidapi (https://github.com/libusb/hidapi). Install via:
#   brew install hidapi          (macOS)
#   apt install libhidapi-dev    (Debian/Ubuntu)
#   vcpkg install hidapi         (Windows)

cmake_minimum_required(VERSION 3.13)
project(joypad_minimal_c C)

# Add libjoypad from the parent tree. Skip libjoypad's own tests when included
# this way — examples only care about the library.
set(LIBJOYPAD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(../.. libjoypad_build)

# Locate HIDAPI via pkg-config (works on macOS/Linux). On Windows, set
# HIDAPI_INCLUDE_DIR and HIDAPI_LIBRARY manually or use vcpkg toolchain.
find_package(PkgConfig)
if(PkgConfig_FOUND)
    # Try hidapi-hidraw first (preferred on Linux), then fall back to hidapi
    # (macOS, Windows, BSDs). pkg_check_modules with multiple args means AND,
    # not OR, so we have to try them separately.
    pkg_check_modules(HIDAPI hidapi-hidraw)
    if(NOT HIDAPI_FOUND)
        pkg_check_modules(HIDAPI hidapi)
    endif()
endif()

add_executable(joypad_minimal_c main.c)
target_link_libraries(joypad_minimal_c PRIVATE libjoypad)
target_compile_options(joypad_minimal_c PRIVATE -Wall -Wextra)

if(HIDAPI_FOUND)
    target_include_directories(joypad_minimal_c PRIVATE ${HIDAPI_INCLUDE_DIRS})
    target_link_libraries(joypad_minimal_c PRIVATE ${HIDAPI_LIBRARIES})
    target_link_directories(joypad_minimal_c PRIVATE ${HIDAPI_LIBRARY_DIRS})
else()
    # Fallback: assume HIDAPI is on the default search paths.
    target_link_libraries(joypad_minimal_c PRIVATE hidapi)
endif()
