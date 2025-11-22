# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(pico-psx-controller-bitbang C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(pico-psx-controller-bitbang
    src/main.c
    src/psx_protocol.c
    src/psx_bitbang.c
    src/button_input.c
    src/shared_state.c
    src/flash_config.c
)

# Include directories
target_include_directories(pico-psx-controller-bitbang PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

pico_set_program_name(pico-psx-controller-bitbang "PSX Controller BitBang")
pico_set_program_version(pico-psx-controller-bitbang "1.0")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(pico-psx-controller-bitbang 0)
pico_enable_stdio_usb(pico-psx-controller-bitbang 1)

# Add the standard library to the build
target_link_libraries(pico-psx-controller-bitbang
        pico_stdlib
        pico_multicore
        hardware_gpio
        hardware_timer
        hardware_flash
        hardware_sync
)

# Add the standard include files to the build
target_include_directories(pico-psx-controller-bitbang PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(pico-psx-controller-bitbang)

