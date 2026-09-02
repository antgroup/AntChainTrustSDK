# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

set(package_prefix "${CMAKE_CURRENT_BINARY_DIR}/package-install")
set(package_build "${CMAKE_CURRENT_BINARY_DIR}/package-consumer")
set(sdk_build "${ACTRUST_PACKAGE_BUILD_DIR}")
set(dependency_source "${CMAKE_CURRENT_LIST_DIR}/dependencies")
set(dependency_build "${CMAKE_CURRENT_BINARY_DIR}/package-dependencies")

file(
    REMOVE_RECURSE
    "${package_prefix}"
    "${package_build}"
    "${dependency_build}"
)

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -S "${dependency_source}" -B "${dependency_build}"
        "-DACTRUST_SDK_BUILD_DIR=${sdk_build}"
    RESULT_VARIABLE dependency_result
)
if(NOT dependency_result EQUAL 0)
    message(FATAL_ERROR "Reusable third-party dependency setup failed")
endif()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" --install "${sdk_build}" --prefix "${package_prefix}"
    RESULT_VARIABLE install_result
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "SDK package installation failed")
endif()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_LIST_DIR}/consumer" -B
        "${package_build}" "-DCMAKE_PREFIX_PATH=${package_prefix}"
        "-DACTRUST_EXTERNAL_DEPENDENCY_DIR=${dependency_source}"
        "-DACTRUST_SDK_BUILD_DIR=${sdk_build}"
        "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    RESULT_VARIABLE configure_result
)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "SDK package consumer configuration failed")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${package_build}"
    RESULT_VARIABLE build_result
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "SDK package consumer build failed")
endif()

execute_process(
    COMMAND
        "${CMAKE_CTEST_COMMAND}" --test-dir "${package_build}"
        --output-on-failure
    RESULT_VARIABLE test_result
)
if(NOT test_result EQUAL 0)
    message(FATAL_ERROR "SDK package consumer test failed")
endif()

file(
    GLOB_RECURSE installed_headers
    LIST_DIRECTORIES false
    "${package_prefix}/include/*"
)
foreach(header IN LISTS installed_headers)
    if(NOT header MATCHES "/include/actrust(_errno)?\\.h$")
        message(FATAL_ERROR "Unexpected installed public header: ${header}")
    endif()
endforeach()

file(GLOB installed_archives LIST_DIRECTORIES false "${package_prefix}/lib/*.a")
list(LENGTH installed_archives installed_archive_count)
if(NOT installed_archive_count EQUAL 1)
    message(
        FATAL_ERROR
        "Expected exactly one installed SDK archive, found "
        "${installed_archive_count}"
    )
endif()
if(NOT installed_archives MATCHES "/lib/libAntChainTrustSDK\\.a$")
    message(
        FATAL_ERROR
        "Unexpected installed SDK archive: ${installed_archives}"
    )
endif()

foreach(
    unexpected_archive
    IN
    ITEMS
        libunity.a
        libmbedcrypto.a
        libmbedx509.a
        libmbedtls.a
        libcore_mqtt.a
        libcore_mqtt_agent.a
        libcore_json.a
        libcore_sntp.a
        libbackoff_algorithm.a
)
    if(EXISTS "${package_prefix}/lib/${unexpected_archive}")
        message(
            FATAL_ERROR
            "Third-party archive '${unexpected_archive}' leaked into SDK package"
        )
    endif()
endforeach()

foreach(unexpected_package IN ITEMS unity MbedTLS)
    if(EXISTS "${package_prefix}/lib/cmake/${unexpected_package}")
        message(
            FATAL_ERROR
            "Third-party package '${unexpected_package}' leaked into SDK package"
        )
    endif()
endforeach()
