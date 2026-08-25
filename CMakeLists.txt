cmake_minimum_required(VERSION 3.16)

if(TUYAOPEN_PLATFORM_SIWX917_LIB_DIR)
    message(STATUS "TUYAOPEN_PLATFORM_SIWX917_LIB_DIR = ${TUYAOPEN_PLATFORM_SIWX917_LIB_DIR}")
    # Add the platform library directory
    add_subdirectory(${TUYAOPEN_PLATFORM_SIWX917_LIB_DIR} ${TUYAOPEN_PLATFORM_SIWX917_BIN_DIR})
else()
    message(FATAL_ERROR "No platform library directory found")
endif()

if(EXISTS ${BUILD_PARAM_PATH}/build_param.cmake)
    include(${BUILD_PARAM_PATH}/build_param.cmake)
else()
    message(FATAL_ERROR "${BUILD_PARAM_PATH}/build_param.cmake not found")
endif()

project(TuyaOpen)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Set global compiler flags
add_compile_options(-fdiagnostics-color=always)

message(STATUS "BIN_OUTPUT_DIR= ${BIN_OUTPUT_DIR}")

if(CONFIG_ENABLE_WIFI STREQUAL "y")
    set(CONFIG_ENABLE_WIFI ON)
else()
    set(CONFIG_ENABLE_WIFI OFF)
endif()

if(CONFIG_ENABLE_BLUETOOTH STREQUAL "y")
    set(CONFIG_ENABLE_BLUETOOTH ON)
else()
    set(CONFIG_ENABLE_BLUETOOTH OFF)
endif()

if(CONFIG_ENABLE_WIRED STREQUAL "y")
    set(CONFIG_ENABLE_WIRED ON)
else()
    set(CONFIG_ENABLE_WIRED OFF)
endif()

file(GLOB_RECURSE UTILITIES_SRC "${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/utilities/*.c")
set(ADAPTER_SRC
    "${CMAKE_SOURCE_DIR}/mcu/src/app_tuya.c" 
    "${CMAKE_SOURCE_DIR}/mcu/src/startup-gcc.c"
    "${CMAKE_SOURCE_DIR}/mcu/src/syscalls.c"
    "${CMAKE_SOURCE_DIR}/mcu/src/psram_heap_4.c"

    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_output.c"  
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_system.c" 
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_sleep.c" 
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_memory.c"  
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_mutex.c" 
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_semaphore.c" 
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_queue.c"  
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_thread.c"

    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_gpio.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_uart.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_i2c.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_spi.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_pinmux.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_timer.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_flash.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_i2s.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_fs.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_lwip.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_network.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_ota.c"  
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_vad.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_kws.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/sl_tuya_display.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_symmetry.c"
)

# See platform_config.cmake for why MP3_MALLOC/MP3_FREE point here: this just
# adds the object file to the final link.
if(CONFIG_MP3_DECODER_STATIC_BUF STREQUAL "y")
    list(APPEND ADAPTER_SRC "${CMAKE_SOURCE_DIR}/mcu/src/mp3_internal_pool.c")
endif()

set(INIT_SRC "${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/init/src/tkl_init_common.c")

if(CONFIG_ENABLE_WIFI)
    set(WIFI_SRC
        "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_wifi.c")
    list(APPEND INIT_SRC 
        "${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/init/src/tkl_init_wifi.c")

    # tkl_wifi.c calls dhcps_start/stop/set_new_lease_cb for SoftAP; pull in
    # lwIP's dhcpserver applet from src/liblwip only when that's needed.
    # Declared in boards/SiWx917/TKL_Kconfig, not src/liblwip, since no other
    # platform references these symbols.
    if(CONFIG_ENABLE_LWIP_DHCPD STREQUAL "y")
        set(DHCPD_SRC "${CMAKE_SOURCE_DIR}/../../src/liblwip/lwip-2.1.2/src/apps/dhcpserver/dhcpserver.c")
        list(APPEND WIFI_SRC ${DHCPD_SRC})
        # Vendor lwIP code isn't written to satisfy this SLC project's
        # -Wall -Werror; src/liblwip/CMakeLists.txt applies the same -w to it.
        set_source_files_properties(${DHCPD_SRC} PROPERTIES COMPILE_OPTIONS "-w")
    endif()
endif()

if(CONFIG_ENABLE_BLUETOOTH)
    file(GLOB_RECURSE BT_SRC "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_bt.c")
endif()

if(CONFIG_ENABLE_WIRED)
    list(APPEND INIT_SRC "${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/init/src/tkl_init_wired.c")
endif()

set(OS_ADAPTER_SOURCES 
    ${UTILITIES_SRC} 
    ${ADAPTER_SRC} 
    ${INIT_SRC}
    ${WIFI_SRC}
    ${BT_SRC}
)

file(GLOB_RECURSE ADAPTER_INCLUDE_SUB_DIRS LIST_DIRECTORIES true "${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/*")
list(FILTER ADAPTER_INCLUDE_SUB_DIRS INCLUDE REGEX ".*/$")
foreach(dir ${ADAPTER_INCLUDE_SUB_DIRS})
    if(IS_DIRECTORY ${dir})
        include_directories(${dir})
    endif()
endforeach()

get_target_property(SLCC_INCLUDE_DIRS  ${TUYAOPEN_PLATFORM_SIWX917} INCLUDE_DIRECTORIES)

include_directories(
    ${SLCC_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/mcu/component
    ${CMAKE_SOURCE_DIR}/mcu/src
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/adc
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/asr
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/bluetooth
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/config
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/display
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/flash
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/gpio
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/hci
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/i2c
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/i2s
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/init/include
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/kws
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/media
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/network
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/pinmux
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/pwm
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/rtc
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/security
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/spi
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/system
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/timer
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/uart
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/utilities/include
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/vad
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/watchdog
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/wifi
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/wired
)

if(EXISTS ${OPEN_ROOT}/boards/SiWx917/common/CMakeLists.txt)
    include(${OPEN_ROOT}/boards/SiWx917/common/CMakeLists.txt)
    list(APPEND OS_ADAPTER_SOURCES ${BOARD_SRC})
    include_directories(${BOARD_INC})
endif()

message(STATUS "OPEN_LIBS_DIR:   ${OPEN_LIBS_DIR}")
message(STATUS "OPEN_HEADER_DIR: ${OPEN_HEADER_DIR}")

if(OPEN_HEADER_DIR)
    string(REPLACE ";" " " HEADER_DIR_LIST "${OPEN_HEADER_DIR}")
    separate_arguments(HEADER_DIR_LIST)
    foreach(header_dir ${HEADER_DIR_LIST})
        include_directories(${header_dir})
    endforeach()
endif()

add_executable(${CONFIG_PROJECT_NAME} ${OS_ADAPTER_SOURCES})

target_link_libraries(${CONFIG_PROJECT_NAME}
    -L${OPEN_LIBS_DIR}
    ${TUYAOPEN_PLATFORM_SIWX917}
    -Wl,-Map=${CONFIG_PROJECT_NAME}.map
)

message(STATUS "CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")
set_target_properties( ${CONFIG_PROJECT_NAME} PROPERTIES OUTPUT_NAME "${CONFIG_PROJECT_NAME}.elf")
set(COMMANDER ${CMAKE_SOURCE_DIR}/tools/commander/commander)
set(postbuild_cmd ${COMMANDER} postbuild ${CMAKE_SOURCE_DIR}/slc/platform_projects/tuyaopen_si91x_template.slpb --parameter build_dir:${CMAKE_BINARY_DIR} --parameter project_name:${CONFIG_PROJECT_NAME})
add_custom_command(TARGET ${CONFIG_PROJECT_NAME}
    POST_BUILD
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMAND ${postbuild_cmd}
)

add_custom_command(
    TARGET ${CONFIG_PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_SIZE} -A ${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_PROJECT_NAME}.elf
)

if(PLATFORM_NEED_LIBS)
    message(STATUS "++++ LIBS_LIST: ${LIBS_LIST}")
    string(REPLACE " " ";" LIBS_LIST "${PLATFORM_NEED_LIBS}")
    target_link_libraries(${CONFIG_PROJECT_NAME}
        ${LIBS_LIST}
    )
endif()
