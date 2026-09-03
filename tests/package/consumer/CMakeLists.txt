# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.16)

project(AntChainTrustSDKPackageConsumer LANGUAGES C)

include(CTest)

set(ACTRUST_EXTERNAL_DEPENDENCY_DIR
    ""
    CACHE PATH
    "Directory containing reusable third-party CMake targets"
)
if(NOT IS_DIRECTORY "${ACTRUST_EXTERNAL_DEPENDENCY_DIR}")
    message(
        FATAL_ERROR
        "Set ACTRUST_EXTERNAL_DEPENDENCY_DIR to reusable third-party targets"
    )
endif()
add_subdirectory("${ACTRUST_EXTERNAL_DEPENDENCY_DIR}" external-dependencies)

find_package(AntChainTrustSDK CONFIG REQUIRED)

add_executable(actrust_package_consumer consumer.c)
target_link_libraries(actrust_package_consumer PRIVATE AntChainTrustSDK::core)

add_test(NAME actrust_package_consumer COMMAND actrust_package_consumer)
