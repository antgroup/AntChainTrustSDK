# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED ACTRUST_ARCHIVE_OUTPUT OR ACTRUST_ARCHIVE_OUTPUT STREQUAL "")
    message(FATAL_ERROR "ACTRUST_ARCHIVE_OUTPUT is required")
endif()
if(NOT DEFINED ACTRUST_ARCHIVE_AR OR ACTRUST_ARCHIVE_AR STREQUAL "")
    message(FATAL_ERROR "ACTRUST_ARCHIVE_AR is required")
endif()
if(NOT DEFINED ACTRUST_ARCHIVE_RANLIB OR ACTRUST_ARCHIVE_RANLIB STREQUAL "")
    message(FATAL_ERROR "ACTRUST_ARCHIVE_RANLIB is required")
endif()
if(NOT DEFINED ACTRUST_ARCHIVE_COUNT)
    message(FATAL_ERROR "ACTRUST_ARCHIVE_COUNT is required")
endif()

get_filename_component(output_dir "${ACTRUST_ARCHIVE_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
set(work_dir "${output_dir}/AntChainTrustSDK-archive")
file(REMOVE_RECURSE "${work_dir}")
file(MAKE_DIRECTORY "${work_dir}")

set(objects)
math(EXPR last_index "${ACTRUST_ARCHIVE_COUNT} - 1")
foreach(index RANGE ${last_index})
    set(input_archive "${ACTRUST_ARCHIVE_${index}}")
    if(NOT EXISTS "${input_archive}")
        message(
            FATAL_ERROR
            "SDK archive input does not exist: ${input_archive}"
        )
    endif()

    set(extract_dir "${work_dir}/${index}")
    file(MAKE_DIRECTORY "${extract_dir}")
    execute_process(
        COMMAND "${ACTRUST_ARCHIVE_AR}" x "${input_archive}"
        WORKING_DIRECTORY "${extract_dir}"
        RESULT_VARIABLE extract_result
        OUTPUT_VARIABLE extract_output
        ERROR_VARIABLE extract_error
    )
    if(NOT extract_result EQUAL 0)
        message(
            FATAL_ERROR
            "Failed to extract SDK archive '${input_archive}' with "
            "${ACTRUST_ARCHIVE_AR}: ${extract_error}"
        )
    endif()

    file(GLOB extracted_objects LIST_DIRECTORIES false "${extract_dir}/*")
    list(APPEND objects ${extracted_objects})
endforeach()

if(NOT objects)
    message(FATAL_ERROR "No objects found in SDK component archives")
endif()

file(REMOVE "${ACTRUST_ARCHIVE_OUTPUT}")
execute_process(
    COMMAND "${ACTRUST_ARCHIVE_AR}" qc "${ACTRUST_ARCHIVE_OUTPUT}" ${objects}
    RESULT_VARIABLE archive_result
    OUTPUT_VARIABLE archive_output
    ERROR_VARIABLE archive_error
)
if(NOT archive_result EQUAL 0)
    message(
        FATAL_ERROR
        "Failed to create SDK archive '${ACTRUST_ARCHIVE_OUTPUT}' with "
        "${ACTRUST_ARCHIVE_AR}: ${archive_error}"
    )
endif()

execute_process(
    COMMAND "${ACTRUST_ARCHIVE_RANLIB}" "${ACTRUST_ARCHIVE_OUTPUT}"
    RESULT_VARIABLE ranlib_result
    OUTPUT_VARIABLE ranlib_output
    ERROR_VARIABLE ranlib_error
)
if(NOT ranlib_result EQUAL 0)
    message(
        FATAL_ERROR
        "Failed to index SDK archive '${ACTRUST_ARCHIVE_OUTPUT}' with "
        "${ACTRUST_ARCHIVE_RANLIB}: ${ranlib_error}"
    )
endif()
