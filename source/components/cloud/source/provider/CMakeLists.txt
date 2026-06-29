# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK Cloud Providers
# =============================================================================
# Adds the selected cloud provider implementation to the parent cloud target.
# =============================================================================

if(
    DEFINED CONFIG_ACTRUST_CLOUD_PLATFORM_AWS
    AND CONFIG_ACTRUST_CLOUD_PLATFORM_AWS
)
    set(CLOUD_PROVIDER_AWS_DIR "${CMAKE_CURRENT_LIST_DIR}/aws")

    target_sources(
        cloud
        PRIVATE
            "${CLOUD_PROVIDER_AWS_DIR}/source/cloud_aws.c"
            "${CLOUD_PROVIDER_AWS_DIR}/source/cloud_aws_callback.c"
    )

    target_include_directories(cloud PUBLIC "${CLOUD_PROVIDER_AWS_DIR}/include")
endif()
