# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# CMake Toolchain File for generic ARM Linux
# =============================================================================
# Cross-compilation toolchain for ARM Linux targets that use the existing Linux
# adapter implementation.  The default GNU triplet is arm-linux-gnueabihf-.
#
# Usage:
#   export ACTRUST_ARM_LINUX_TOOLCHAIN_PATH=/path/to/toolchain/root
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-linux-arm.cmake ..
#
# The toolchain may also be discovered from PATH.  Override the triplet with
# ACTRUST_ARM_LINUX_TOOLCHAIN_PREFIX, for example:
#   export ACTRUST_ARM_LINUX_TOOLCHAIN_PREFIX=arm-none-linux-gnueabihf-
#
# Optional:
#   ACTRUST_ARM_LINUX_SYSROOT     Explicit sysroot path.
#   ACTRUST_ARM_LINUX_ARCH_FLAGS  Extra CPU/ABI flags, e.g. "-mcpu=cortex-a7".
# =============================================================================

# -----------------------------------------------------------------------------
# System identification
# -----------------------------------------------------------------------------
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# -----------------------------------------------------------------------------
# Toolchain prefix and optional path
# -----------------------------------------------------------------------------
if(
    NOT DEFINED ACTRUST_ARM_LINUX_TOOLCHAIN_PREFIX
    OR ACTRUST_ARM_LINUX_TOOLCHAIN_PREFIX STREQUAL ""
)
    if(
        DEFINED ENV{ACTRUST_ARM_LINUX_TOOLCHAIN_PREFIX}
        AND NOT "$ENV{ACTRUST_ARM_LINUX_TOOLCHAIN_PREFIX}" STREQUAL ""
    )
        set(ACTRUST_ARM_LINUX_TOOLCHAIN_PREFIX
            "$ENV{ACTRUST_ARM_LINUX_TOOLCHAIN_PREFIX}"
        )
    else()
        set(ACTRUST_ARM_LINUX_TOOLCHAIN_PREFIX "arm-linux-gnueabihf-")
    endif()
endif()

if(
    NOT DEFINED ACTRUST_ARM_LINUX_TOOLCHAIN_PATH
    OR ACTRUST_ARM_LINUX_TOOLCHAIN_PATH STREQUAL ""
)
    if(
        DEFINED ENV{ACTRUST_ARM_LINUX_TOOLCHAIN_PATH}
        AND NOT "$ENV{ACTRUST_ARM_LINUX_TOOLCHAIN_PATH}" STREQUAL ""
    )
        set(ACTRUST_ARM_LINUX_TOOLCHAIN_PATH
            "$ENV{ACTRUST_ARM_LINUX_TOOLCHAIN_PATH}"
        )
    endif()
endif()

set(TOOLCHAIN_PREFIX "${ACTRUST_ARM_LINUX_TOOLCHAIN_PREFIX}")

if(
    DEFINED ACTRUST_ARM_LINUX_TOOLCHAIN_PATH
    AND NOT ACTRUST_ARM_LINUX_TOOLCHAIN_PATH STREQUAL ""
)
    get_filename_component(
        ACTRUST_ARM_LINUX_TOOLCHAIN_PATH
        "${ACTRUST_ARM_LINUX_TOOLCHAIN_PATH}"
        ABSOLUTE
    )
    set(TOOLCHAIN_DIR "${ACTRUST_ARM_LINUX_TOOLCHAIN_PATH}")
    set(CMAKE_C_COMPILER "${TOOLCHAIN_DIR}/bin/${TOOLCHAIN_PREFIX}gcc")

    if(NOT EXISTS "${CMAKE_C_COMPILER}")
        message(
            FATAL_ERROR
            "ARM Linux cross-compiler not found: ${CMAKE_C_COMPILER}\n"
            "Set ACTRUST_ARM_LINUX_TOOLCHAIN_PATH to the toolchain root "
            "(the directory containing bin/${TOOLCHAIN_PREFIX}gcc)."
        )
    endif()
else()
    find_program(ACTRUST_ARM_LINUX_CC NAMES "${TOOLCHAIN_PREFIX}gcc")
    if(NOT ACTRUST_ARM_LINUX_CC)
        set(TOOLCHAIN_DIR "${CMAKE_CURRENT_LIST_DIR}/../toolchain/arm-linux")
        set(CMAKE_C_COMPILER "${TOOLCHAIN_DIR}/bin/${TOOLCHAIN_PREFIX}gcc")
        if(NOT EXISTS "${CMAKE_C_COMPILER}")
            message(
                FATAL_ERROR
                "ARM Linux cross-compiler not found in PATH: "
                "${TOOLCHAIN_PREFIX}gcc\n"
                "Set ACTRUST_ARM_LINUX_TOOLCHAIN_PATH or install a toolchain "
                "providing ${TOOLCHAIN_PREFIX}gcc."
            )
        endif()
    else()
        set(CMAKE_C_COMPILER "${ACTRUST_ARM_LINUX_CC}")
        get_filename_component(
            TOOLCHAIN_BIN_DIR
            "${CMAKE_C_COMPILER}"
            DIRECTORY
        )
        get_filename_component(TOOLCHAIN_DIR "${TOOLCHAIN_BIN_DIR}" DIRECTORY)
    endif()
endif()

# -----------------------------------------------------------------------------
# Cross-compilers and binutils
# -----------------------------------------------------------------------------
function(actrust_find_arm_linux_tool out_var suffix)
    if(EXISTS "${TOOLCHAIN_DIR}/bin/${TOOLCHAIN_PREFIX}${suffix}")
        set("${out_var}"
            "${TOOLCHAIN_DIR}/bin/${TOOLCHAIN_PREFIX}${suffix}"
            PARENT_SCOPE
        )
    else()
        find_program(_tool_path NAMES "${TOOLCHAIN_PREFIX}${suffix}")
        if(_tool_path)
            set("${out_var}" "${_tool_path}" PARENT_SCOPE)
        endif()
        unset(_tool_path CACHE)
    endif()
endfunction()

actrust_find_arm_linux_tool(CMAKE_CXX_COMPILER g++)
actrust_find_arm_linux_tool(CMAKE_ASM_COMPILER gcc)
actrust_find_arm_linux_tool(CMAKE_AR ar)
actrust_find_arm_linux_tool(CMAKE_RANLIB ranlib)
actrust_find_arm_linux_tool(CMAKE_STRIP strip)
actrust_find_arm_linux_tool(CMAKE_OBJCOPY objcopy)
actrust_find_arm_linux_tool(CMAKE_OBJDUMP objdump)
actrust_find_arm_linux_tool(CMAKE_NM nm)
actrust_find_arm_linux_tool(CMAKE_LINKER ld)

# -----------------------------------------------------------------------------
# Optional sysroot
# -----------------------------------------------------------------------------
if(
    NOT DEFINED ACTRUST_ARM_LINUX_SYSROOT
    OR ACTRUST_ARM_LINUX_SYSROOT STREQUAL ""
)
    if(
        DEFINED ENV{ACTRUST_ARM_LINUX_SYSROOT}
        AND NOT "$ENV{ACTRUST_ARM_LINUX_SYSROOT}" STREQUAL ""
    )
        set(ACTRUST_ARM_LINUX_SYSROOT "$ENV{ACTRUST_ARM_LINUX_SYSROOT}")
    elseif(EXISTS "${TOOLCHAIN_DIR}/sysroot")
        set(ACTRUST_ARM_LINUX_SYSROOT "${TOOLCHAIN_DIR}/sysroot")
    endif()
endif()

if(
    DEFINED ACTRUST_ARM_LINUX_SYSROOT
    AND NOT ACTRUST_ARM_LINUX_SYSROOT STREQUAL ""
)
    get_filename_component(
        ACTRUST_ARM_LINUX_SYSROOT
        "${ACTRUST_ARM_LINUX_SYSROOT}"
        ABSOLUTE
    )
    set(CMAKE_SYSROOT "${ACTRUST_ARM_LINUX_SYSROOT}")
    set(CMAKE_FIND_ROOT_PATH "${ACTRUST_ARM_LINUX_SYSROOT}")
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
endif()

# -----------------------------------------------------------------------------
# Optional architecture flags
# -----------------------------------------------------------------------------
if(
    NOT DEFINED ACTRUST_ARM_LINUX_ARCH_FLAGS
    OR ACTRUST_ARM_LINUX_ARCH_FLAGS STREQUAL ""
)
    if(
        DEFINED ENV{ACTRUST_ARM_LINUX_ARCH_FLAGS}
        AND NOT "$ENV{ACTRUST_ARM_LINUX_ARCH_FLAGS}" STREQUAL ""
    )
        set(ACTRUST_ARM_LINUX_ARCH_FLAGS "$ENV{ACTRUST_ARM_LINUX_ARCH_FLAGS}")
    endif()
endif()

if(
    DEFINED ACTRUST_ARM_LINUX_ARCH_FLAGS
    AND NOT ACTRUST_ARM_LINUX_ARCH_FLAGS STREQUAL ""
)
    string(APPEND CMAKE_C_FLAGS_INIT " ${ACTRUST_ARM_LINUX_ARCH_FLAGS}")
    string(APPEND CMAKE_CXX_FLAGS_INIT " ${ACTRUST_ARM_LINUX_ARCH_FLAGS}")
endif()

# -----------------------------------------------------------------------------
# Security hardening
# -----------------------------------------------------------------------------
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,-z,now -Wl,-z,relro")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-Wl,-z,now -Wl,-z,relro")
