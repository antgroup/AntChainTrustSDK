#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLANG_FORMAT_CONFIG="${ROOT_DIR}/.clang-format"
MODE="write"

usage() {
    cat <<EOF
Usage: $(basename "$0") [--check]

Format tracked and unignored project files with clang-format, shfmt, and
gersemi.

Options:
  --check    Verify formatting without modifying files.
  -h, --help Show this help message.
EOF
}

case "${1:-}" in
"") ;;
--check)
    MODE="check"
    ;;
-h | --help)
    usage
    exit 0
    ;;
*)
    echo "Unknown option: $1" >&2
    usage >&2
    exit 2
    ;;
esac

if [[ $# -gt 1 ]]; then
    echo "Too many arguments." >&2
    usage >&2
    exit 2
fi

[[ -f ${CLANG_FORMAT_CONFIG} ]] || {
    echo "Missing .clang-format at ${CLANG_FORMAT_CONFIG}" >&2
    exit 1
}

command -v clang-format >/dev/null 2>&1 || {
    echo "clang-format not found in PATH" >&2
    exit 1
}

command -v shfmt >/dev/null 2>&1 || {
    echo "shfmt not found in PATH" >&2
    exit 1
}

command -v gersemi >/dev/null 2>&1 || {
    echo "gersemi not found in PATH" >&2
    exit 1
}

command -v git >/dev/null 2>&1 || {
    echo "git not found in PATH" >&2
    exit 1
}

cd "${ROOT_DIR}"

run_git_files() {
    git ls-files -z --cached --others --exclude-standard -- "$@"
}

if [[ ${MODE} == "check" ]]; then
    run_git_files "*.c" "*.h" ":(exclude)3rdparts/**" |
        xargs -0 -r clang-format --dry-run --Werror -style=file

    run_git_files "*.sh" ":(exclude)3rdparts/**" |
        xargs -0 -r shfmt -d -s -i 4

    run_git_files ":(glob)**/CMakeLists.txt" "*.cmake" ":(exclude)3rdparts/**" |
        xargs -0 -r gersemi --check --no-cache -l 80 --indent 4 \
            --no-warn-about-unknown-commands

    echo "Format check passed."
else
    run_git_files "*.c" "*.h" ":(exclude)3rdparts/**" |
        xargs -0 -r clang-format -i -style=file

    run_git_files "*.sh" ":(exclude)3rdparts/**" |
        xargs -0 -r shfmt -w -s -i 4

    run_git_files ":(glob)**/CMakeLists.txt" "*.cmake" ":(exclude)3rdparts/**" |
        xargs -0 -r gersemi -i --no-cache -l 80 --indent 4 \
            --no-warn-about-unknown-commands

    echo "Format completed."
fi
