cmake_minimum_required(VERSION 3.13)

# Select the Pico 2 W target before the SDK initializes it.
set(PICO_BOARD pico2_w)
set(PICO_PLATFORM rp2350)

# Configure the Pico SDK before project()
include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)

project(buslink_gpib_adapter C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(buslink_gpib_adapter
    main.c
)

target_include_directories(buslink_gpib_adapter
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Pull in standard Pico libraries
target_link_libraries(buslink_gpib_adapter
    PRIVATE
        pico_stdlib
        pico_stdio_usb
        pico_time
)

# Enable USB CDC support for the adapter
pico_enable_stdio_usb(buslink_gpib_adapter 1)
pico_enable_stdio_uart(buslink_gpib_adapter 0)

# USB IDs: pid.codes / USB-IF assignment used for this adapter
# Vendor ID: 0x1209
# Product ID: 0x5305
add_compile_definitions(USBD_VID=0x1209 USBD_PID=0x5305)

pico_add_extra_outputs(buslink_gpib_adapter)
