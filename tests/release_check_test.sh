#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

python3 "$ROOT_DIR/tools/release/check_release.py" --root "$ROOT_DIR"
python3 "$ROOT_DIR/tests/release/check_release_test.py"

baseline_tag="$(python3 "$ROOT_DIR/tools/release/check_release.py" \
    --root "$ROOT_DIR" --print-baseline-tag \
    --release-tag "v$(<"$ROOT_DIR/VERSION")" --baseline-ref HEAD)"
python3 "$ROOT_DIR/tools/release/check_release.py" --root "$ROOT_DIR" \
    --api --baseline-tag "$baseline_tag"

printf 'Release check tests passed.\n'
