# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

set(_actrust_dependencies
    "backoffAlgorithm|backoffAlgorithm|v1.4.2|14f4c88b33dd554be30a00a312c88d3986d457d0|MIT|https://github.com/FreeRTOS/backoffAlgorithm"
    "coreJSON|coreJSON|v3.3.1|cffa492da18c890181d64462f8af63992a69d3b0|MIT|https://github.com/FreeRTOS/coreJSON"
    "coreMQTT|coreMQTT|v2.3.1|2beef04725328923e05e576b884212d53ec97af7|MIT|https://github.com/FreeRTOS/coreMQTT"
    "coreMQTT-Agent|coreMQTT-Agent|v1.3.1|e977d70ee68c95f94d967ea18feadfffe5c7a584|MIT|https://github.com/FreeRTOS/coreMQTT-Agent"
    "coreSNTP|coreSNTP|v2.0.0|50f5f96f4c33b14c0358f404ff4ff2a29d422ad7|MIT|https://github.com/FreeRTOS/coreSNTP"
    "mbedTLS|mbedtls|v3.6.5|e185d7fd85499c8ce5ca2a54f5cf8fe7dbe3f8df|Apache-2.0|https://github.com/Mbed-TLS/mbedtls"
)
foreach(_actrust_dependency IN LISTS _actrust_dependencies)
    string(REPLACE "|" ";" _actrust_fields "${_actrust_dependency}")
    list(GET _actrust_fields 0 _actrust_name)
    list(GET _actrust_fields 1 _actrust_directory)
    list(GET _actrust_fields 3 _actrust_expected_sha)
    set(_actrust_path "${ACTRUST_SOURCE_DIR}/3rdparts/${_actrust_directory}")
    if(NOT EXISTS "${_actrust_path}/.git")
        message(FATAL_ERROR "Dependency checkout missing: ${_actrust_path}")
    endif()
    execute_process(
        COMMAND git -C "${_actrust_path}" rev-parse HEAD
        OUTPUT_VARIABLE _actrust_actual_sha
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _actrust_git_result
    )
    if(NOT _actrust_git_result EQUAL 0 OR NOT _actrust_actual_sha STREQUAL _actrust_expected_sha)
        message(FATAL_ERROR "Dependency SHA mismatch for ${_actrust_name}")
    endif()
endforeach()

string(
    CONCAT _actrust_manifest
    "{\n"
    "  \"schema\": \"AntChainTrustSDK third-party manifest v1\",\n"
    "  \"sdk_version\": \"${ACTRUST_SDK_VERSION}\",\n"
    "  \"dependencies\": [\n"
    "    {\"name\": \"backoffAlgorithm\", \"version\": \"v1.4.2\", \"git_sha\": \"14f4c88b33dd554be30a00a312c88d3986d457d0\", \"license\": \"MIT\", \"url\": \"https://github.com/FreeRTOS/backoffAlgorithm\"},\n"
    "    {\"name\": \"coreJSON\", \"version\": \"v3.3.1\", \"git_sha\": \"cffa492da18c890181d64462f8af63992a69d3b0\", \"license\": \"MIT\", \"url\": \"https://github.com/FreeRTOS/coreJSON\"},\n"
    "    {\"name\": \"coreMQTT\", \"version\": \"v2.3.1\", \"git_sha\": \"2beef04725328923e05e576b884212d53ec97af7\", \"license\": \"MIT\", \"url\": \"https://github.com/FreeRTOS/coreMQTT\"},\n"
    "    {\"name\": \"coreMQTT-Agent\", \"version\": \"v1.3.1\", \"git_sha\": \"e977d70ee68c95f94d967ea18feadfffe5c7a584\", \"license\": \"MIT\", \"url\": \"https://github.com/FreeRTOS/coreMQTT-Agent\"},\n"
    "    {\"name\": \"coreSNTP\", \"version\": \"v2.0.0\", \"git_sha\": \"50f5f96f4c33b14c0358f404ff4ff2a29d422ad7\", \"license\": \"MIT\", \"url\": \"https://github.com/FreeRTOS/coreSNTP\"},\n"
    "    {\"name\": \"mbedTLS\", \"version\": \"v3.6.5\", \"git_sha\": \"e185d7fd85499c8ce5ca2a54f5cf8fe7dbe3f8df\", \"license\": \"Apache-2.0\", \"url\": \"https://github.com/Mbed-TLS/mbedtls\"}\n"
    "  ]\n"
    "}\n"
)

file(MAKE_DIRECTORY "${ACTRUST_OUTPUT_DIR}")
file(WRITE "${ACTRUST_OUTPUT_DIR}/THIRD_PARTY.json" "${_actrust_manifest}")

string(
    CONCAT _actrust_spdx
    "{\n"
    "  \"spdxVersion\": \"SPDX-2.3\",\n"
    "  \"dataLicense\": \"CC0-1.0\",\n"
    "  \"SPDXID\": \"SPDXRef-DOCUMENT\",\n"
    "  \"name\": \"AntChainTrustSDK-${ACTRUST_SDK_VERSION}\",\n"
    "  \"documentNamespace\": \"https://antchain.example.invalid/spdx/AntChainTrustSDK-${ACTRUST_SDK_VERSION}\",\n"
    "  \"creationInfo\": {\"created\": \"1970-01-01T00:00:00Z\", \"creators\": [\"Organization: Antchain (SHANGHAI) Digital Technology Co., Ltd.\"]},\n"
    "  \"packages\": [\n"
    "    {\"SPDXID\": \"SPDXRef-Package-AntChainTrustSDK\", \"name\": \"AntChainTrustSDK\", \"versionInfo\": \"${ACTRUST_SDK_VERSION}\", \"downloadLocation\": \"NOASSERTION\", \"licenseConcluded\": \"Apache-2.0\", \"licenseDeclared\": \"Apache-2.0\"},\n"
    "    {\"SPDXID\": \"SPDXRef-Package-backoffAlgorithm\", \"name\": \"backoffAlgorithm\", \"versionInfo\": \"v1.4.2\", \"downloadLocation\": \"https://github.com/FreeRTOS/backoffAlgorithm\", \"licenseConcluded\": \"MIT\", \"licenseDeclared\": \"MIT\"},\n"
    "    {\"SPDXID\": \"SPDXRef-Package-coreJSON\", \"name\": \"coreJSON\", \"versionInfo\": \"v3.3.1\", \"downloadLocation\": \"https://github.com/FreeRTOS/coreJSON\", \"licenseConcluded\": \"MIT\", \"licenseDeclared\": \"MIT\"},\n"
    "    {\"SPDXID\": \"SPDXRef-Package-coreMQTT\", \"name\": \"coreMQTT\", \"versionInfo\": \"v2.3.1\", \"downloadLocation\": \"https://github.com/FreeRTOS/coreMQTT\", \"licenseConcluded\": \"MIT\", \"licenseDeclared\": \"MIT\"},\n"
    "    {\"SPDXID\": \"SPDXRef-Package-coreMQTT-Agent\", \"name\": \"coreMQTT-Agent\", \"versionInfo\": \"v1.3.1\", \"downloadLocation\": \"https://github.com/FreeRTOS/coreMQTT-Agent\", \"licenseConcluded\": \"MIT\", \"licenseDeclared\": \"MIT\"},\n"
    "    {\"SPDXID\": \"SPDXRef-Package-coreSNTP\", \"name\": \"coreSNTP\", \"versionInfo\": \"v2.0.0\", \"downloadLocation\": \"https://github.com/FreeRTOS/coreSNTP\", \"licenseConcluded\": \"MIT\", \"licenseDeclared\": \"MIT\"},\n"
    "    {\"SPDXID\": \"SPDXRef-Package-mbedTLS\", \"name\": \"mbedTLS\", \"versionInfo\": \"v3.6.5\", \"downloadLocation\": \"https://github.com/Mbed-TLS/mbedtls\", \"licenseConcluded\": \"Apache-2.0\", \"licenseDeclared\": \"Apache-2.0\"}\n"
    "  ]\n"
    "}\n"
)
file(WRITE "${ACTRUST_OUTPUT_DIR}/sbom.spdx.json" "${_actrust_spdx}")
