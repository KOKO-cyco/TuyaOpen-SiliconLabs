list_subdirectories(PLATFORM_PUBINC_1 ${PLATFORM_PATH}/tuyaos_adapter)
include(${TOP_SOURCE_DIR}/boards/SiWx917/common/CMakeLists.txt)

set(PLATFORM_PUBINC_2
    ${BOARD_INC})

# Prefer SiWx917 PSRAM-aware tuya_ringbuf.h over tools/porting template
# (template maps OVERFLOW_PSRAM_* to normal types and breaks large AI buffers).
set(PLATFORM_PUBINC
    ${PLATFORM_PATH}/tuyaos_adapter/include/utilities/include
    ${PLATFORM_PUBINC_1}
    ${PLATFORM_PUBINC_2})

# Add global definition for SLI_SI917B0 macro
add_definitions(-DSLI_SI917B0)

# The MP3 decoder's scratch buffer is rewritten throughout every frame; this
# board's general purpose heap is in PSRAM (see tkl_memory.c), which can't
# keep up. Point the decoder's MP3_MALLOC/MP3_FREE hooks
# (src/audio_player/.../minimp3.h, unmodified from upstream's #ifndef
# MP3_MALLOC / #else structure) at this platform's own allocator via
# -include: forcing mp3_malloc_platform.h at the top of every translation
# unit has the exact same preprocessor effect as -DMP3_MALLOC=..., just as an
# actual (greppable) file instead of a raw command-line string.
#
# Must be defined here, not in ./CMakeLists.txt: the same define added there
# once compiled decoder_mp3.c.obj against tal_psram_malloc regardless (verified
# via nm), i.e. it never reached src/'s compile flags, only this platform's own
# adapter sources. This file is include()d from the root CMakeLists.txt before
# add_subdirectory(src/...), which does reach it (same place SLI_SI917B0
# above is defined). Also must use PLATFORM_PATH, not CMAKE_SOURCE_DIR: this
# file runs in the TOP-LEVEL (TuyaOpen-root) CMake project, where
# CMAKE_SOURCE_DIR is the repo root, not platform/SiWx917 -- unlike
# ./CMakeLists.txt, which is its own separate platform-rooted CMake project
# (built into apps/*/.build/your_chat_bot/) where CMAKE_SOURCE_DIR does mean
# platform/SiWx917. Confirmed via nm the hard way: CMAKE_SOURCE_DIR here
# resolved to a nonexistent path, which -I silently skips with no build error,
# and decoder_mp3.c.obj fell through to tal_psram_malloc unnoticed.
if(CONFIG_MP3_DECODER_STATIC_BUF STREQUAL "y")
    add_compile_options(-include ${PLATFORM_PATH}/mcu/include/mp3_malloc_platform.h)
endif()

set(CMAKE_BUILD_TYPE Release)

# WARNING: Changing CMAKE_BUILD_TYPE from Release to Debug may significantly impact
# performance on the SiWx917 platform, potentially causing voice quality degradation,
# audio dropouts, or real-time processing issues. Use Debug builds only for
# development purposes and ensure thorough testing of audio/voice functionality.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(WARNING "DEBUG BUILD DETECTED: SiWx917 platform performance may be insufficient "
                    "for optimal voice quality. Audio dropouts and processing delays may occur. "
                    "Consider using Release build for voice-enabled applications.")
endif()

# Generate the SLC project. Driven from here rather than through tos.py's
# build_setup hook because the only extra thing the generator needs is the app
# build directory, which CMake already has as CMAKE_CURRENT_BINARY_DIR; routing
# it through tos.py would mean adding a fifth positional argument to a
# build_setup signature that every platform shares. Re-running is cheap:
# script/generate skips an output dir that already exists.
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env python slc_generate.py
            "${CONFIG_PROJECT_NAME}" "${CMAKE_CURRENT_BINARY_DIR}"
    WORKING_DIRECTORY "${PLATFORM_PATH}"
    RESULT_VARIABLE SLC_GENERATE_RESULT)

if(NOT SLC_GENERATE_RESULT EQUAL 0)
    message(FATAL_ERROR "[Platform] slc_generate.py failed (${SLC_GENERATE_RESULT})")
endif()

# Use auto-generated include directories from SLC
# This file is created by slc_generate.py after SLC generation with full absolute paths
set(SLC_INCLUDES_FILE "${CMAKE_CURRENT_BINARY_DIR}/slc_includes.cmake")

if(EXISTS "${SLC_INCLUDES_FILE}")
    include("${SLC_INCLUDES_FILE}")
    message(STATUS "[Platform] Using auto-generated SLC includes (${SLC_INCLUDE_DIRS})")
    set(HW_CRYPTO_INCLUDES ${SLC_INCLUDE_DIRS})
else()
    message(WARNING "[Platform] Auto-generated includes not found: ${SLC_INCLUDES_FILE}")
    set(HW_CRYPTO_INCLUDES "")
endif()

# Hardware crypto interface library
add_library(si91x_sdk_includes INTERFACE)
target_include_directories(si91x_sdk_includes INTERFACE ${HW_CRYPTO_INCLUDES})

target_compile_definitions(si91x_sdk_includes
    INTERFACE
        USE_SI91X_HW_CRYPTO=1
)

# Export to global scope (no PARENT_SCOPE at root level)
set(PLATFORM_SI91X_SDK_INCLUCES si91x_sdk_includes CACHE INTERNAL "Platform Si91x SDK includes")