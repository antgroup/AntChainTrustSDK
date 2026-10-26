#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# Never trace secret material, even if the caller enables shell tracing.
set +x
set -euo pipefail

if [[ $# -ne 3 ]]; then
    printf 'Usage: %s <input-file> <signature-file> <public-key-file>\n' \
        "$(basename "$0")" >&2
    exit 2
fi

if [[ -z ${ACTRUST_SIGNING_PRIVATE_KEY:-} ||
    -z ${ACTRUST_SIGNING_PASSPHRASE:-} ]]; then
    printf 'Release signing private key and passphrase are required.\n' >&2
    exit 1
fi
if [[ ! -s $3 ]]; then
    printf 'Release verification public key is missing.\n' >&2
    exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
umask 077
key_dir="$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/actrust-signing.XXXXXX")"
cleanup() {
    rm -rf -- "$key_dir"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

export ACTRUST_RELEASE_PRIVATE_KEY_FILE="${key_dir}/private.pem"
export ACTRUST_RELEASE_PASSPHRASE_FILE="${key_dir}/passphrase.txt"
export ACTRUST_RELEASE_PUBLIC_KEY_FILE="$3"
printf '%s\n' "$ACTRUST_SIGNING_PRIVATE_KEY" >"$ACTRUST_RELEASE_PRIVATE_KEY_FILE"
printf '%s\n' "$ACTRUST_SIGNING_PASSPHRASE" >"$ACTRUST_RELEASE_PASSPHRASE_FILE"
unset ACTRUST_SIGNING_PRIVATE_KEY ACTRUST_SIGNING_PASSPHRASE

"${script_dir}/sign_artifact.sh" "$1" "$2"
