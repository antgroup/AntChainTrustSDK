#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

if [[ $# -ne 2 ]]; then
    printf 'Usage: %s <input-file> <signature-file>\n' "$(basename "$0")" >&2
    exit 2
fi

input_file="$1"
signature_file="$2"

if [[ ! -f $input_file ]]; then
    printf 'Signing input does not exist: %s\n' "$input_file" >&2
    exit 1
fi

if [[ -z ${ACTRUST_SIGN_COMMAND:-} ]]; then
    printf 'ACTRUST_SIGN_COMMAND is required for a production release.\n' >&2
    exit 1
fi
if [[ -z ${ACTRUST_VERIFY_COMMAND:-} ]]; then
    printf 'ACTRUST_VERIFY_COMMAND is required for a production release.\n' >&2
    exit 1
fi

rm -f -- "$signature_file"
export ACTRUST_SIGN_INPUT="$input_file"
export ACTRUST_SIGN_OUTPUT="$signature_file"
export ACTRUST_VERIFY_INPUT="$input_file"
export ACTRUST_VERIFY_SIGNATURE="$signature_file"

# The configured command must read ACTRUST_SIGN_INPUT and write
# ACTRUST_SIGN_OUTPUT. Key material remains owned by the CI environment.
bash -euo pipefail -c "$ACTRUST_SIGN_COMMAND"

if [[ ! -s $signature_file ]]; then
    printf 'Signing command did not produce a signature: %s\n' "$signature_file" >&2
    exit 1
fi

# The verifier must cryptographically validate the signature against the exact
# input file. Key and signer identity remain owned by the CI environment.
bash -euo pipefail -c "$ACTRUST_VERIFY_COMMAND"
