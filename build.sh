#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK Build Script
# =============================================================================
# This script automates the build process for the AntChainTrustSDK project.
# It handles configuration generation, CMake setup, compilation, and testing.
#
# Usage: ./build.sh [OPTIONS]
# See --help for available options.
# =============================================================================

set -euo pipefail

# -----------------------------------------------------------------------------
# Configuration
# -----------------------------------------------------------------------------

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

# Platform defconfig directory
CONFIGS_DIR="$ROOT_DIR/config"
CONFIG_FILE="$ROOT_DIR/.config"
SUPPORTED_PLATFORMS=(android linux_x86 linux_arm simcom_a7606e)

# Flags
BUILD_TESTS=0
RUN_TESTS=0
TEST_LABEL="actrust"
CLEAN=0
CLEAN_BUILD=0
VERBOSE=0
SKIP_CONFIG=0
PLATFORM=""
BUILD_TARGET=""

# Color output support (if terminal supports it)
if [[ -t 1 ]] && command -v tput >/dev/null 2>&1; then
    BOLD=$(tput bold)
    GREEN=$(tput setaf 2)
    YELLOW=$(tput setaf 3)
    RED=$(tput setaf 1)
    RESET=$(tput sgr0)
else
    BOLD="" GREEN="" YELLOW="" RED="" RESET=""
fi

# -----------------------------------------------------------------------------
# Helper Functions
# -----------------------------------------------------------------------------

log_info() {
    echo "${GREEN}==>${RESET} ${BOLD}$*${RESET}"
}

log_warn() {
    echo "${YELLOW}Warning:${RESET} $*" >&2
}

log_error() {
    echo "${RED}Error:${RESET} $*" >&2
}

config_enabled() {
    local key="$1"
    [[ -f $CONFIG_FILE ]] && grep -Eq "^${key}=y([[:space:]]*(#.*)?)?$" "$CONFIG_FILE"
}

platforms_csv() {
    local platforms=""
    local platform
    for platform in "${SUPPORTED_PLATFORMS[@]}"; do
        platforms="${platforms:+$platforms, }$platform"
    done
    echo "$platforms"
}

print_platforms() {
    local platform
    for platform in "${SUPPORTED_PLATFORMS[@]}"; do
        echo "  $platform"
    done
}

platform_defconfig_name() {
    case "$1" in
    android)
        echo "android"
        ;;
    linux_x86 | linux_arm)
        echo "linux"
        ;;
    simcom_a7606e)
        echo "simcom_a7606e"
        ;;
    *)
        return 1
        ;;
    esac
}

uses_simcom_toolchain() {
    if [[ $BUILD_TARGET == "simcom_a7606e" ]]; then
        return 0
    fi
    if [[ -z $BUILD_TARGET ]]; then
        config_enabled CONFIG_ACTRUST_ADAPTER_PLATFORM_SIMCOM_A7606E
        return $?
    fi
    return 1
}

uses_linux_arm_toolchain() {
    [[ $BUILD_TARGET == "linux_arm" ]]
}

uses_android_toolchain() {
    if [[ $BUILD_TARGET == "android" ]]; then
        return 0
    fi
    if [[ -z $BUILD_TARGET ]]; then
        config_enabled CONFIG_ACTRUST_ADAPTER_PLATFORM_ANDROID
        return $?
    fi
    return 1
}

is_cross_compiled_build() {
    uses_simcom_toolchain ||
        uses_linux_arm_toolchain ||
        uses_android_toolchain
}

usage() {
    local platforms
    platforms="$(platforms_csv)"

    cat <<EOF
${BOLD}Usage:${RESET} ./build.sh [PLATFORM] [OPTIONS]

${BOLD}Description:${RESET}
    Build the AntChainTrustSDK project with optional configuration generation,
    testing, and cleanup operations.

${BOLD}Platform:${RESET}
    An optional platform name that selects a build target and defconfig.
    linux_x86 and linux_arm both copy config/linux_defconfig; linux_arm also
    enables the ARM Linux toolchain.
    If omitted, the existing .config is used as-is.
    Available: ${platforms:-<none>}

${BOLD}Options:${RESET}
    --clean           Remove build directory and exit
    --clean-build     Remove build directory before building
    --test            Build and run all actrust tests
    --test-adapter    Build and run adapter tests only
    --test-component  Build and run component tests only
    --build-tests     Build tests without running them
    --skip-config     Skip configuration generation step
    --verbose, -v     Enable verbose output during build
    --help, -h        Show this help message

${BOLD}Environment Variables:${RESET}
    BUILD_DIR         Build directory path (default: $ROOT_DIR/build)
    BUILD_TYPE        CMake build type: Debug|Release|RelWithDebInfo|MinSizeRel
                      (default: Debug)
    JOBS              Number of parallel build jobs (default: auto-detected)
    ACTRUST_TOOLCHAIN_PATH
                      SIMCom OpenWrt toolchain root containing
                      bin/arm-openwrt-linux-muslgnueabi-gcc
    ACTRUST_ARM_LINUX_TOOLCHAIN_PATH
                      ARM Linux toolchain root containing bin/<triplet>gcc
    ACTRUST_ARM_LINUX_TOOLCHAIN_PREFIX
                      ARM Linux GNU triplet (default: arm-linux-gnueabihf-)
    ACTRUST_ARM_LINUX_SYSROOT
                      Optional ARM Linux sysroot path
    ACTRUST_ARM_LINUX_ARCH_FLAGS
                      Optional CPU/ABI flags for ARM Linux
    ANDROID_NDK_HOME or ANDROID_NDK_ROOT
                      Android NDK root for Android builds
    ANDROID_ABI       Android ABI (default: arm64-v8a)
    ANDROID_PLATFORM  Android API level (default: android-23)

${BOLD}Examples:${RESET}
    # Build for host Linux with tests
    ./build.sh linux_x86 --clean-build --test

    # Cross-compile for generic ARM Linux
    ./build.sh linux_arm --clean-build

    # Cross-compile for SIMCom A7606E
    ./build.sh simcom_a7606e --clean-build

    # Cross-compile for Android with the Android NDK
    ANDROID_NDK_HOME=/path/to/android-ndk ./build.sh android

    # Use existing .config (backward compatible)
    ./build.sh --clean-build --test

    # Release build with 8 parallel jobs
    BUILD_TYPE=Release JOBS=8 ./build.sh linux_x86

EOF
}

# -----------------------------------------------------------------------------
# Argument Parsing
# -----------------------------------------------------------------------------

while [[ $# -gt 0 ]]; do
    case "$1" in
    --clean)
        CLEAN=1
        shift
        ;;
    --clean-build)
        CLEAN_BUILD=1
        shift
        ;;
    --test)
        BUILD_TESTS=1
        RUN_TESTS=1
        shift
        ;;
    --test-adapter)
        BUILD_TESTS=1
        RUN_TESTS=1
        TEST_LABEL="adapter"
        shift
        ;;
    --test-component)
        BUILD_TESTS=1
        RUN_TESTS=1
        TEST_LABEL="component"
        shift
        ;;
    --build-tests)
        BUILD_TESTS=1
        shift
        ;;
    --skip-config)
        SKIP_CONFIG=1
        shift
        ;;
    --verbose | -v)
        VERBOSE=1
        shift
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    -*)
        log_error "Unknown option: $1"
        echo ""
        usage
        exit 1
        ;;
    *)
        # Positional argument: platform name
        if [[ -n $PLATFORM ]]; then
            log_error "Multiple platforms specified: '$PLATFORM' and '$1'"
            exit 1
        fi
        PLATFORM="$1"
        shift
        ;;
    esac
done

# -----------------------------------------------------------------------------
# Main Build Process
# -----------------------------------------------------------------------------

# Handle clean operation
if [[ $CLEAN -eq 1 ]]; then
    if [[ -d $BUILD_DIR ]]; then
        log_info "Removing build directory: $BUILD_DIR"
        rm -rf "$BUILD_DIR"
        log_info "Clean complete"
    else
        log_info "Build directory does not exist, nothing to clean"
    fi
    exit 0
fi

# Handle clean-build operation
if [[ $CLEAN_BUILD -eq 1 ]]; then
    if [[ -d $BUILD_DIR ]]; then
        log_info "Removing build directory for clean build"
        rm -rf "$BUILD_DIR"
    fi
fi

# Apply platform defconfig if specified
if [[ -n $PLATFORM ]]; then
    BUILD_TARGET="$PLATFORM"
    if ! CONFIG_PLATFORM="$(platform_defconfig_name "$PLATFORM")"; then
        log_error "Unknown platform: $PLATFORM"
        echo ""
        echo "Available platforms:"
        print_platforms
        exit 1
    fi

    DEFCONFIG="$CONFIGS_DIR/${CONFIG_PLATFORM}_defconfig"
    if [[ ! -f $DEFCONFIG ]]; then
        log_error "Defconfig not found: $DEFCONFIG"
        echo ""
        echo "Configured platforms:"
        print_platforms
        exit 1
    fi
    log_info "Applying defconfig: ${CONFIG_PLATFORM}_defconfig (for platform: ${PLATFORM})"
    cp "$DEFCONFIG" "$ROOT_DIR/.config"
fi

# Generate configuration from Kconfig
if [[ $SKIP_CONFIG -eq 0 ]]; then
    log_info "Generating configuration files"
    if ! "$ROOT_DIR/tools/genconfig.sh"; then
        log_error "Configuration generation failed"
        exit 1
    fi
else
    log_warn "Skipping configuration generation (--skip-config)"
fi

# Configure CMake
log_info "Configuring CMake (BUILD_TYPE=${BUILD_TYPE})"
CMAKE_ARGS=(
    -S "$ROOT_DIR"
    -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)

if [[ $BUILD_TESTS -eq 1 ]]; then
    CMAKE_ARGS+=(-DBUILD_TESTING=ON)
else
    CMAKE_ARGS+=(-DBUILD_TESTING=OFF)
fi

# Select cross-compilation toolchain from the requested build target. If no
# platform was passed, fall back to the generated .config for legacy workflows.
if uses_simcom_toolchain; then
    TOOLCHAIN_FILE="$ROOT_DIR/cmake/toolchain-simcom-a7606e.cmake"
    if [[ -f $TOOLCHAIN_FILE ]]; then
        log_info "Using toolchain: toolchain-simcom-a7606e.cmake"
        CMAKE_ARGS+=(-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE")
    else
        log_error "Toolchain file not found: $TOOLCHAIN_FILE"
        exit 1
    fi
elif uses_linux_arm_toolchain; then
    TOOLCHAIN_FILE="$ROOT_DIR/cmake/toolchain-linux-arm.cmake"
    if [[ -f $TOOLCHAIN_FILE ]]; then
        log_info "Using toolchain: toolchain-linux-arm.cmake"
        CMAKE_ARGS+=(-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE")
    else
        log_error "Toolchain file not found: $TOOLCHAIN_FILE"
        exit 1
    fi
elif uses_android_toolchain; then
    NDK_ROOT="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
    if [[ -z $NDK_ROOT ]]; then
        log_error "Android build requires ANDROID_NDK_HOME or ANDROID_NDK_ROOT"
        exit 1
    fi

    TOOLCHAIN_FILE="$NDK_ROOT/build/cmake/android.toolchain.cmake"
    if [[ -f $TOOLCHAIN_FILE ]]; then
        log_info "Using Android NDK toolchain: $TOOLCHAIN_FILE"
        CMAKE_ARGS+=(-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE")
        CMAKE_ARGS+=(-DANDROID_ABI="${ANDROID_ABI:-arm64-v8a}")
        CMAKE_ARGS+=(-DANDROID_PLATFORM="${ANDROID_PLATFORM:-android-23}")
    else
        log_error "Android NDK toolchain file not found: $TOOLCHAIN_FILE"
        exit 1
    fi
fi

if [[ $VERBOSE -eq 1 ]]; then
    CMAKE_ARGS+=(--warn-uninitialized)
fi

if ! cmake "${CMAKE_ARGS[@]}"; then
    log_error "CMake configuration failed"
    exit 1
fi

# Build the project
log_info "Building project (using $JOBS parallel jobs)"
BUILD_ARGS=(
    --build "$BUILD_DIR"
    --parallel "$JOBS"
)

if [[ $VERBOSE -eq 1 ]]; then
    BUILD_ARGS+=(--verbose)
fi

if ! cmake "${BUILD_ARGS[@]}"; then
    log_error "Build failed"
    exit 1
fi

log_info "Build completed successfully"

# -------------------------------------------------------------------------
# Post-build: ROM/RAM size and stack usage report
# -------------------------------------------------------------------------
report_size_and_stack() {
    # Select the correct size tool for cross-builds so the numbers reflect the
    # target binary layout.
    local size_cmd="size"
    if uses_simcom_toolchain; then
        local toolchain_root="${ACTRUST_TOOLCHAIN_PATH:-$ROOT_DIR/toolchain/arm-openwrt-linux}"
        local cross_size="$toolchain_root/bin/arm-openwrt-linux-muslgnueabi-size"
        if [[ -x $cross_size ]]; then
            size_cmd="$cross_size"
        else
            log_warn "cross 'size' not found at $cross_size; using host size"
        fi
    elif uses_android_toolchain; then
        local ndk_root="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
        local llvm_sizes=("$ndk_root"/toolchains/llvm/prebuilt/*/bin/llvm-size)
        if [[ -n $ndk_root && -x ${llvm_sizes[0]:-} ]]; then
            size_cmd="${llvm_sizes[0]}"
        else
            log_warn "Android llvm-size not found; using host size"
        fi
    elif uses_linux_arm_toolchain; then
        local prefix="${ACTRUST_ARM_LINUX_TOOLCHAIN_PREFIX:-}"
        if [[ -z $prefix ]]; then
            prefix="arm-linux-gnueabihf-"
        fi
        local toolchain_root="${ACTRUST_ARM_LINUX_TOOLCHAIN_PATH:-}"
        local cross_size=""

        if [[ -n $toolchain_root ]]; then
            cross_size="$toolchain_root/bin/${prefix}size"
        else
            cross_size="$(command -v "${prefix}size" 2>/dev/null || true)"
        fi

        if [[ -n $cross_size && -x $cross_size ]]; then
            size_cmd="$cross_size"
        else
            log_warn "ARM Linux '${prefix}size' not found; using host size"
        fi
    fi

    # Collect actrust static libraries (exclude 3rdparty)
    local libs
    libs=$(find "$BUILD_DIR/source" -name "*.a" 2>/dev/null | sort)
    if [[ -z $libs ]]; then
        return
    fi

    echo ""
    log_info "ROM/RAM Size Report (text=ROM, data=ROM+RAM, bss=RAM)"
    echo "  ---------------------------------------------------------------"
    printf "  ${BOLD}%-24s %8s %8s %8s %8s${RESET}\n" "Library" "text" "data" "bss" "total"
    echo "  ---------------------------------------------------------------"

    local grand_text=0 grand_data=0 grand_bss=0 grand_total=0
    for lib in $libs; do
        local name
        name="$(basename "$lib")"
        # Get totals line from size -t
        local totals
        totals=$("$size_cmd" -t "$lib" 2>/dev/null | tail -1)
        if [[ -z $totals ]]; then
            continue
        fi
        local t d b total
        t=$(echo "$totals" | awk '{print $1}')
        d=$(echo "$totals" | awk '{print $2}')
        b=$(echo "$totals" | awk '{print $3}')
        total=$(echo "$totals" | awk '{print $4}')
        printf "  %-24s %8s %8s %8s %8s\n" "$name" "$t" "$d" "$b" "$total"
        grand_text=$((grand_text + t))
        grand_data=$((grand_data + d))
        grand_bss=$((grand_bss + b))
        grand_total=$((grand_total + total))
    done
    echo "  ---------------------------------------------------------------"
    printf "  ${BOLD}%-24s %8s %8s %8s %8s${RESET}\n" "TOTAL" "$grand_text" "$grand_data" "$grand_bss" "$grand_total"

    # Stack usage report from .su files
    echo ""
    log_info "Stack Usage Report (from -fstack-usage)"
    echo "  -----------------------------------------------"
    printf "  ${BOLD}%-24s %10s %10s${RESET}\n" "Library" "max(B)" "sum(B)"
    echo "  -----------------------------------------------"

    for lib in $libs; do
        local name target_name
        name="$(basename "$lib" .a)"
        target_name="${name#lib}"
        # Derive the CMakeFiles dir: same parent as the .a, under CMakeFiles/<target>.dir
        local lib_dir
        lib_dir="$(dirname "$lib")/CMakeFiles/${target_name}.dir"
        if [[ ! -d $lib_dir ]]; then
            continue
        fi

        local su_files
        su_files=$(find "$lib_dir" -name "*.su" 2>/dev/null)
        if [[ -z $su_files ]]; then
            continue
        fi

        # Parse all .su entries: extract function, size, type
        local max_sz=0 total_sz=0
        for f in $su_files; do
            while IFS= read -r line; do
                local sz
                sz=$(echo "$line" | awk '{print $2}')
                if [[ -n $sz ]]; then
                    total_sz=$((total_sz + sz))
                    if [[ $sz -gt $max_sz ]]; then
                        max_sz=$sz
                    fi
                fi
            done <"$f"
        done

        printf "  %-24s %10d %10d\n" "${name}.a" "$max_sz" "$total_sz"
    done
    echo "  -----------------------------------------------"
}

report_size_and_stack

# Run tests if requested
if [[ $RUN_TESTS -eq 1 ]]; then
    # Refuse to run tests for cross-compiled builds (binaries can't execute on host)
    if is_cross_compiled_build; then
        log_warn "Cannot run tests: cross-compiled binaries cannot execute on host"
        log_info "Transfer test binaries to the target device to run them"
        exit 1
    fi

    log_info "Running tests (label: ${TEST_LABEL})"

    CTEST_ARGS=(
        --test-dir "$BUILD_DIR"
        --output-on-failure
        --label-regex "$TEST_LABEL"
    )

    if [[ $VERBOSE -eq 1 ]]; then
        CTEST_ARGS+=(--verbose)
    fi

    if ! ctest "${CTEST_ARGS[@]}"; then
        log_error "Tests failed"
        exit 1
    fi

    log_info "All tests passed"
fi

log_info "Done!"
