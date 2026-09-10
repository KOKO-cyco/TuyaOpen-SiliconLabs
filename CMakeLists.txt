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

set(UTILITIES_SRC
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/utilities/src/tkl_log.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/utilities/src/tuya_hashmap.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/utilities/src/tuya_list.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/utilities/src/tuya_mem_heap.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/utilities/src/tuya_queue.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/utilities/src/tuya_ringbuf.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/utilities/src/tuya_smartpointer.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/utilities/src/tuya_tools.c"
)
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
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_asr.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_kws.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_display.c"
    "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_symmetry.c"
)

# See platform_config.cmake for why MP3_MALLOC/MP3_FREE point here: this just
# adds the object file to the final link.
if(CONFIG_MP3_DECODER_STATIC_BUF STREQUAL "y")
    list(APPEND ADAPTER_SRC "${CMAKE_SOURCE_DIR}/mcu/src/mp3_internal_pool.c")
endif()

if(CONFIG_ENABLE_RTC STREQUAL "y")
    list(APPEND ADAPTER_SRC "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_rtc.c")
endif()

if(CONFIG_ENABLE_PWM STREQUAL "y")
    list(APPEND ADAPTER_SRC "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_pwm.c")
endif()

if(CONFIG_ENABLE_ADC STREQUAL "y")
    list(APPEND ADAPTER_SRC "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_adc.c")
endif()

if(CONFIG_ENABLE_DAC STREQUAL "y")
    list(APPEND ADAPTER_SRC "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_dac.c")
endif()

if(CONFIG_ENABLE_WAKEUP STREQUAL "y")
    list(APPEND ADAPTER_SRC "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_wakeup.c")
endif()

set(INIT_SRC "${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/init/src/tkl_init_common.c")

if(CONFIG_ENABLE_WIFI)
    set(WIFI_SRC
        "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_wifi.c")
    list(APPEND INIT_SRC
        "${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/init/src/tkl_init_wifi.c")

    # tkl_wifi.c calls dhcps_start/stop/set_new_lease_cb for SoftAP; pull in
    # lwIP's dhcpserver applet from src/liblwip only when that's needed.
    # Declared in boards/SIWX917/TKL_Kconfig, not src/liblwip, since no other
    # platform references these symbols.
    if(CONFIG_ENABLE_LWIP_DHCPD STREQUAL "y")
        set(DHCPD_SRC "${OPEN_ROOT}/src/liblwip/lwip-2.1.2/src/apps/dhcpserver/dhcpserver.c")
        list(APPEND WIFI_SRC ${DHCPD_SRC})
        # Vendor lwIP code isn't written to satisfy this SLC project's
        # -Wall -Werror; src/liblwip/CMakeLists.txt applies the same -w to it.
        set_source_files_properties(${DHCPD_SRC} PROPERTIES COMPILE_OPTIONS "-w")
    endif()
endif()

if(CONFIG_ENABLE_BLUETOOTH)
    set(BT_SRC "${CMAKE_SOURCE_DIR}/tuyaos_adapter/src/tkl_bt.c")
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

# Every non-hidden directory under tuyaos_adapter/include is an include path.
# Collected here rather than hand-listed: the previous form globbed with
# LIST_DIRECTORIES and then filtered on a ".*/$" regex, which never matches --
# CMake glob results carry no trailing slash -- so that loop was a no-op and a
# hand-maintained list did the real work. Five of its entries (adc, pwm, rtc,
# watchdog, wired) plus mcu/component named directories that do not exist; -I
# on a missing path is silently dropped, so nothing ever complained.
file(GLOB_RECURSE ADAPTER_INCLUDE_ENTRIES LIST_DIRECTORIES true
     "${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/*")
set(ADAPTER_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/tuyaos_adapter/include")
foreach(entry ${ADAPTER_INCLUDE_ENTRIES})
    if(IS_DIRECTORY "${entry}")
        get_filename_component(entry_name "${entry}" NAME)
        if(NOT entry_name MATCHES "^\\.")
            list(APPEND ADAPTER_INCLUDE_DIRS "${entry}")
        endif()
    endif()
endforeach()

get_target_property(SLCC_INCLUDE_DIRS ${TUYAOPEN_PLATFORM_SIWX917} INCLUDE_DIRECTORIES)

include_directories(
    ${SLCC_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/mcu/src
    ${ADAPTER_INCLUDE_DIRS}
)

if(EXISTS ${OPEN_ROOT}/boards/${CONFIG_PLATFORM_CHOICE}/common/CMakeLists.txt)
    include(${OPEN_ROOT}/boards/${CONFIG_PLATFORM_CHOICE}/common/CMakeLists.txt)
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
    string(REPLACE " " ";" LIBS_LIST "${PLATFORM_NEED_LIBS}")
    message(STATUS "++++ LIBS_LIST: ${LIBS_LIST}")
    target_link_libraries(${CONFIG_PROJECT_NAME}
        ${LIBS_LIST}
    )

    # These arrive as bare names ("tuyaapp tuyaos"), so CMake emits -ltuyaapp
    # and nothing more: not a CMake target, not an absolute path, therefore no
    # dependency edge in the generated ninja file. Rebuilding libtuyaapp.a --
    # which is what happens every time application code changes -- then leaves
    # this executable untouched, and the build reports success while the image
    # still holds the previous application. Flashing that is indistinguishable
    # from the flash not having worked.
    #
    # Naming the archives as link dependencies restores the edge without
    # disturbing link order, which the -l flags above still decide. Guarded on
    # EXISTS because a first configure runs before the libraries are built, and
    # a LINK_DEPENDS on a file with no rule to make it is a hard ninja error;
    # by the second build they are there, which is when staleness can occur.
    foreach(_lib ${LIBS_LIST})
        set(_lib_path "${OPEN_LIBS_DIR}/lib${_lib}.a")
        if(EXISTS "${_lib_path}")
            set_property(TARGET ${CONFIG_PROJECT_NAME}
                         APPEND PROPERTY LINK_DEPENDS "${_lib_path}")
        endif()
    endforeach()
endif()
