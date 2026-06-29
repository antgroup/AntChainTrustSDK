# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# CMake Toolchain File for SIMCom A7606E-H (ASR1806 Platform)
# =============================================================================
# Cross-compilation toolchain matching the OpenWrt SDK build environment:
#   - Chip: ASR1806
#   - Arch: ARM Cortex-A7, NEON-VFPv4, hard-float
#   - C Library: musl libc
#   - Toolchain: OpenWrt GCC 13.3.0 (arm-openwrt-linux-muslgnueabi)
#
# Usage:
#   export ACTRUST_TOOLCHAIN_PATH=/path/to/arm-openwrt-linux
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-simcom-a7606e.cmake ..
#
# The toolchain root may also be supplied via the CMake cache variable
# ACTRUST_TOOLCHAIN_PATH. When neither is set, the script falls back to
# <repo>/toolchain/arm-openwrt-linux (useful for developers who symlink
# the toolchain into the tree, but not required for out-of-tree builds).
# =============================================================================

# -----------------------------------------------------------------------------
# System identification
# -----------------------------------------------------------------------------
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# -----------------------------------------------------------------------------
# Toolchain paths
# -----------------------------------------------------------------------------
# Resolution order: cache variable -> environment variable -> in-tree fallback.
# Use CMAKE_CURRENT_LIST_DIR rather than CMAKE_SOURCE_DIR for the fallback so
# the path stays stable during CMake's internal try_compile() phase, where
# CMAKE_SOURCE_DIR temporarily points at a per-attempt scratch directory.
if(NOT DEFINED ACTRUST_TOOLCHAIN_PATH OR ACTRUST_TOOLCHAIN_PATH STREQUAL "")
    if(
        DEFINED ENV{ACTRUST_TOOLCHAIN_PATH}
        AND NOT "$ENV{ACTRUST_TOOLCHAIN_PATH}" STREQUAL ""
    )
        set(ACTRUST_TOOLCHAIN_PATH "$ENV{ACTRUST_TOOLCHAIN_PATH}")
    else()
        set(ACTRUST_TOOLCHAIN_PATH
            "${CMAKE_CURRENT_LIST_DIR}/../toolchain/arm-openwrt-linux"
        )
    endif()
endif()
get_filename_component(
    ACTRUST_TOOLCHAIN_PATH
    "${ACTRUST_TOOLCHAIN_PATH}"
    ABSOLUTE
)

set(TOOLCHAIN_DIR "${ACTRUST_TOOLCHAIN_PATH}")
set(TOOLCHAIN_PREFIX "arm-openwrt-linux-muslgnueabi-")
set(TOOLCHAIN_SYSROOT "${TOOLCHAIN_DIR}/sysroots-target-arm")

if(NOT EXISTS "${TOOLCHAIN_DIR}/bin/${TOOLCHAIN_PREFIX}gcc")
    message(
        FATAL_ERROR
        "ARM cross-compiler not found: ${TOOLCHAIN_DIR}/bin/${TOOLCHAIN_PREFIX}gcc\n"
        "Set ACTRUST_TOOLCHAIN_PATH (env or -D) to the root of the OpenWrt toolchain "
        "(the directory containing bin/${TOOLCHAIN_PREFIX}gcc)."
    )
endif()

# -----------------------------------------------------------------------------
# Cross-compilers
# -----------------------------------------------------------------------------
set(CMAKE_C_COMPILER "${TOOLCHAIN_DIR}/bin/${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_DIR}/bin/${TOOLCHAIN_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_DIR}/bin/${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_AR "${TOOLCHAIN_DIR}/bin/${TOOLCHAIN_PREFIX}ar")
set(CMAKE_RANLIB "${TOOLCHAIN_DIR}/bin/${TOOLCHAIN_PREFIX}ranlib")
set(CMAKE_STRIP "${TOOLCHAIN_DIR}/bin/${TOOLCHAIN_PREFIX}strip")
set(CMAKE_OBJCOPY "${TOOLCHAIN_DIR}/bin/${TOOLCHAIN_PREFIX}objcopy")
set(CMAKE_OBJDUMP "${TOOLCHAIN_DIR}/bin/${TOOLCHAIN_PREFIX}objdump")
set(CMAKE_NM "${TOOLCHAIN_DIR}/bin/${TOOLCHAIN_PREFIX}nm")
set(CMAKE_LINKER "${TOOLCHAIN_DIR}/bin/${TOOLCHAIN_PREFIX}ld")

# -----------------------------------------------------------------------------
# Sysroot
# -----------------------------------------------------------------------------
set(CMAKE_SYSROOT "${TOOLCHAIN_SYSROOT}")
set(CMAKE_STAGING_PREFIX "${TOOLCHAIN_DIR}")

# -----------------------------------------------------------------------------
# Compiler / Linker flags (matching OpenWrt SDK Makefile + AntChainTrustSDK hardening)
# -----------------------------------------------------------------------------
# Architectural & OpenWrt-mandated flags:
#   -Os                         Size optimization
#   -pipe                       Use pipes instead of temp files
#   -fno-caller-saves           Disable caller-saves optimization
#   -mcpu=cortex-a7             Target CPU
#   -mfpu=neon-vfpv4            FPU type
#   -mfloat-abi=hard            Hard-float ABI
#
# Hardening flags (also applied via source/CMakeLists.txt for the AntChainTrustSDK
# sources; duplicated here so vendored 3rdparts/ code is also built with them
# when targeting this platform):
#   -fstack-protector-strong    Modern stack smashing protection
#   -D_FORTIFY_SOURCE=2         Compile- and run-time buffer overflow checks
#   -Wformat=2                  Strict format string checking
#   -Werror=format-security     Reject non-literal format strings
# -----------------------------------------------------------------------------
set(CMAKE_C_FLAGS_INIT
    "-Os \
     -pipe \
     -mcpu=cortex-a7 \
     -mfpu=neon-vfpv4 \
     -mfloat-abi=hard \
     -fno-caller-saves \
     -fstack-protector-strong \
     -D_FORTIFY_SOURCE=2 \
     -Wformat=2 \
     -Werror=format-security"
)

set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")

set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-Wl,-z,now -Wl,-z,relro -Wl,-rpath-link,${TOOLCHAIN_DIR}/lib"
)
set(CMAKE_SHARED_LINKER_FLAGS_INIT
    "-Wl,-z,now -Wl,-z,relro -Wl,-rpath-link,${TOOLCHAIN_DIR}/lib"
)

# -----------------------------------------------------------------------------
# OpenWrt environment
# -----------------------------------------------------------------------------
set(CMAKE_C_COMPILER_LAUNCHER env "STAGING_DIR=${TOOLCHAIN_SYSROOT}")
set(CMAKE_C_LINKER_LAUNCHER env "STAGING_DIR=${TOOLCHAIN_SYSROOT}")

# -----------------------------------------------------------------------------
# Search path configuration
# -----------------------------------------------------------------------------
set(CMAKE_FIND_ROOT_PATH "${TOOLCHAIN_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
