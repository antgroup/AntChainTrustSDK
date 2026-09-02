# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

if(
    NOT DEFINED ACTRUST_PACKAGE_BUILD_DIR
    OR NOT IS_DIRECTORY "${ACTRUST_PACKAGE_BUILD_DIR}"
)
    message(FATAL_ERROR "ACTRUST_PACKAGE_BUILD_DIR must name a build directory")
endif()

set(_actrust_prefix "${ACTRUST_PACKAGE_BUILD_DIR}/bundle-install")
set(_actrust_archive
    "${ACTRUST_PACKAGE_BUILD_DIR}/AntChainTrustSDK-1.0.0-linux_x86.tar.gz"
)
set(_actrust_checksum "${_actrust_archive}.sha256")
file(REMOVE_RECURSE "${_actrust_prefix}")
file(REMOVE "${_actrust_archive}" "${_actrust_checksum}")

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" --install "${ACTRUST_PACKAGE_BUILD_DIR}" --prefix
        "${_actrust_prefix}"
    RESULT_VARIABLE _actrust_install_result
)
if(NOT _actrust_install_result EQUAL 0)
    message(FATAL_ERROR "SDK bundle install tree generation failed")
endif()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" --build "${ACTRUST_PACKAGE_BUILD_DIR}" --target
        actrust_sdk_bundle
    RESULT_VARIABLE _actrust_cpack_result
)
if(NOT _actrust_cpack_result EQUAL 0)
    message(FATAL_ERROR "SDK bundle generation failed")
endif()

if(NOT EXISTS "${_actrust_archive}")
    message(FATAL_ERROR "Expected SDK bundle was not generated")
endif()

file(SHA256 "${_actrust_archive}" _actrust_digest)
if(NOT EXISTS "${_actrust_checksum}")
    message(FATAL_ERROR "SDK bundle checksum file was not generated")
endif()
file(READ "${_actrust_checksum}" _actrust_checksum_text)
string(STRIP "${_actrust_checksum_text}" _actrust_checksum_text)
string(REPLACE "  " ";" _actrust_checksum_fields "${_actrust_checksum_text}")
list(LENGTH _actrust_checksum_fields _actrust_checksum_field_count)
if(NOT _actrust_checksum_field_count EQUAL 2)
    message(FATAL_ERROR "SDK bundle checksum format is invalid")
endif()
list(GET _actrust_checksum_fields 0 _actrust_checksum_value)
list(GET _actrust_checksum_fields 1 _actrust_checksum_name)
get_filename_component(_actrust_archive_name "${_actrust_archive}" NAME)
if(NOT _actrust_checksum_value STREQUAL _actrust_digest)
    message(FATAL_ERROR "SDK bundle checksum digest is invalid")
endif()
if(NOT _actrust_checksum_name STREQUAL _actrust_archive_name)
    message(FATAL_ERROR "SDK bundle checksum filename is invalid")
endif()
file(SHA256 "${_actrust_archive}" _actrust_verified_digest)
if(NOT _actrust_digest STREQUAL _actrust_verified_digest)
    message(FATAL_ERROR "SDK bundle SHA256 verification failed")
endif()

execute_process(
    COMMAND tar -tzf "${_actrust_archive}"
    RESULT_VARIABLE _actrust_tar_result
    OUTPUT_VARIABLE _actrust_members
)
if(NOT _actrust_tar_result EQUAL 0)
    message(FATAL_ERROR "SDK bundle archive cannot be listed")
endif()

foreach(
    _actrust_required
    IN
    ITEMS
        "/include/actrust.h"
        "/include/actrust_errno.h"
        "/lib/libAntChainTrustSDK.a"
        "/share/AntChainTrustSDK/LICENSE"
        "/share/AntChainTrustSDK/NOTICE"
        "/share/AntChainTrustSDK/THIRD_PARTY.json"
        "/share/AntChainTrustSDK/sbom.spdx.json"
        "/share/AntChainTrustSDK/licenses/backoffAlgorithm-LICENSE"
        "/share/AntChainTrustSDK/licenses/coreJSON-LICENSE"
        "/share/AntChainTrustSDK/licenses/coreMQTT-LICENSE"
        "/share/AntChainTrustSDK/licenses/coreMQTT-Agent-LICENSE"
        "/share/AntChainTrustSDK/licenses/coreSNTP-LICENSE"
        "/share/AntChainTrustSDK/licenses/mbedTLS-LICENSE"
)
    if(
        NOT _actrust_members MATCHES "AntChainTrustSDK-[^/]+${_actrust_required}"
    )
        message(FATAL_ERROR "SDK bundle is missing '${_actrust_required}'")
    endif()
endforeach()

foreach(
    _actrust_forbidden
    IN
    ITEMS
        "CMakeCache.txt"
        "compile_commands.json"
        ".actrust/"
        ".key"
        ".crt"
        ".csr"
)
    if(_actrust_members MATCHES "${_actrust_forbidden}")
        message(
            FATAL_ERROR
            "Forbidden '${_actrust_forbidden}' found in SDK bundle"
        )
    endif()
endforeach()

file(
    READ "${ACTRUST_PACKAGE_BUILD_DIR}/bundle-metadata/THIRD_PARTY.json"
    _actrust_manifest
)
foreach(
    _actrust_dependency
    IN
    ITEMS
        "backoffAlgorithm"
        "coreJSON"
        "coreMQTT"
        "coreMQTT-Agent"
        "coreSNTP"
        "mbedTLS"
)
    if(
        NOT _actrust_manifest MATCHES "\\\"name\\\": \\\"${_actrust_dependency}\\\""
    )
        message(
            FATAL_ERROR
            "Dependency '${_actrust_dependency}' missing from manifest"
        )
    endif()
endforeach()

file(
    READ "${ACTRUST_PACKAGE_BUILD_DIR}/bundle-metadata/sbom.spdx.json"
    _actrust_sbom
)
if(NOT _actrust_sbom MATCHES "SPDX-2\\.3")
    message(FATAL_ERROR "SDK SBOM is not SPDX 2.3")
endif()
if(NOT _actrust_sbom MATCHES "SPDXRef-Package-mbedTLS")
    message(FATAL_ERROR "SDK SBOM is missing mbedTLS")
endif()
