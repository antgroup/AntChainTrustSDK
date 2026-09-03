# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED ACTRUST_BUNDLE_ARCHIVE OR ACTRUST_BUNDLE_ARCHIVE STREQUAL "")
    message(FATAL_ERROR "ACTRUST_BUNDLE_ARCHIVE is required")
endif()
if(NOT DEFINED ACTRUST_BUNDLE_CHECKSUM OR ACTRUST_BUNDLE_CHECKSUM STREQUAL "")
    message(FATAL_ERROR "ACTRUST_BUNDLE_CHECKSUM is required")
endif()
if(NOT EXISTS "${ACTRUST_BUNDLE_ARCHIVE}")
    message(FATAL_ERROR "SDK bundle archive does not exist")
endif()

file(SHA256 "${ACTRUST_BUNDLE_ARCHIVE}" _actrust_digest)
get_filename_component(_actrust_archive_name "${ACTRUST_BUNDLE_ARCHIVE}" NAME)
file(
    WRITE "${ACTRUST_BUNDLE_CHECKSUM}"
    "${_actrust_digest}  ${_actrust_archive_name}\n"
)
