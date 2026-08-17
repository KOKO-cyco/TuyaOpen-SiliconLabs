# set toolchain
#
# The directory carries ARM's host triple (x86_64, darwin-arm64, ...), so it
# differs between machines -- see install_arm_toolchain in script/bootstrap.
# Resolve it rather than naming it, which also survives a version bump.
#
# Anchored on CMAKE_CURRENT_LIST_DIR (this file sits in the platform root) and
# not on PLATFORM_PATH: CMake re-includes the toolchain file inside try_compile
# sub-projects, which never receive the -DPLATFORM_PATH from the outer
# configure, so that variable is empty there.
file(GLOB TOOLCHAIN_CANDIDATES
     "${CMAKE_CURRENT_LIST_DIR}/tools/toolchain/arm-gnu-toolchain-*-arm-none-eabi")
if(NOT TOOLCHAIN_CANDIDATES)
    message(FATAL_ERROR
        "No ARM toolchain found under ${CMAKE_CURRENT_LIST_DIR}/tools/toolchain.\n"
        "Install it with: ./script/bootstrap arm_toolchain")
endif()
list(LENGTH TOOLCHAIN_CANDIDATES TOOLCHAIN_COUNT)
if(TOOLCHAIN_COUNT GREATER 1)
    message(WARNING
        "Several ARM toolchains found; using the first of: ${TOOLCHAIN_CANDIDATES}")
endif()
list(GET TOOLCHAIN_CANDIDATES 0 TOOLCHAIN_ROOT)

set(TOOLCHAIN_DIR ${TOOLCHAIN_ROOT}/bin)
set(TOOLCHAIN_PRE "arm-none-eabi-")

# The tools carry a .exe on a Windows host. CMake will not guess that for a
# compiler given as an explicit path -- it checks the name as written and
# reports "is not a full path to an existing compiler tool" -- so spell the
# suffix out. CMAKE_HOST_WIN32 and not CMAKE_EXECUTABLE_SUFFIX: the latter
# describes what this toolchain produces (bare ELF for the target), while what
# matters here is the host running the compiler.
if(CMAKE_HOST_WIN32)
    set(TOOLCHAIN_EXT ".exe")
else()
    set(TOOLCHAIN_EXT "")
endif()

set(CMAKE_SYSTEM_NAME              Generic)
set(CMAKE_SYSTEM_PROCESSOR         ARM)

set(CMAKE_C_COMPILER               "${TOOLCHAIN_DIR}/${TOOLCHAIN_PRE}gcc${TOOLCHAIN_EXT}")
set(CMAKE_CXX_COMPILER             "${TOOLCHAIN_DIR}/${TOOLCHAIN_PRE}g++${TOOLCHAIN_EXT}")
set(CMAKE_ASM_COMPILER             "${TOOLCHAIN_DIR}/${TOOLCHAIN_PRE}gcc${TOOLCHAIN_EXT}")
set(CMAKE_RANLIB                   "${TOOLCHAIN_DIR}/${TOOLCHAIN_PRE}ranlib${TOOLCHAIN_EXT}")
set(CMAKE_AR                       "${TOOLCHAIN_DIR}/${TOOLCHAIN_PRE}ar${TOOLCHAIN_EXT}")
set(CMAKE_SIZE                     "${TOOLCHAIN_DIR}/${TOOLCHAIN_PRE}size${TOOLCHAIN_EXT}")

execute_process(COMMAND ${CMAKE_CXX_COMPILER} -dumpversion OUTPUT_VARIABLE COMPILER_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)

set(COMMON_C_FLAGS                 "-mcpu=cortex-m4 -mthumb -fmessage-length=0 -ffunction-sections -fdata-sections -mfpu=fpv4-sp-d16 -mfloat-abi=softfp")

set(CMAKE_C_FLAGS_INIT             "${COMMON_C_FLAGS} -std=c18")
set(CMAKE_CXX_FLAGS_INIT           "${COMMON_C_FLAGS} -std=c++17 -fno-exceptions -fno-rtti")
set(CMAKE_ASM_FLAGS_INIT           "${CMAKE_C_FLAGS_INIT} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${COMMON_C_FLAGS} -specs=nosys.specs")

set(CMAKE_C_FLAGS_DEBUG            "-Og -g")
set(CMAKE_CXX_FLAGS_DEBUG          "-Og -g")
set(CMAKE_ASM_FLAGS_DEBUG          "-g")

set(CMAKE_C_FLAGS_RELEASE          "-Os")
set(CMAKE_CXX_FLAGS_RELEASE        "-Os")
set(CMAKE_ASM_FLAGS_RELEASE        "")
