cmake_minimum_required(VERSION 3.16)

# Dynamic project name based on CONFIG_APP (default: bt2usb)
if(NOT DEFINED CONFIG_APP)
    set(CONFIG_APP "bt2usb")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(joypad_${CONFIG_APP})
