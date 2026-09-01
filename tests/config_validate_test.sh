#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VALIDATOR="$ROOT_DIR/tools/config_validate.py"
GENERATOR="$ROOT_DIR/tools/genconfig.sh"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

assert_rejected() {
    local name="$1"
    local source="$2"
    if python3 "$VALIDATOR" "$source" >"$TMP_DIR/$name.out" 2>&1; then
        printf 'expected rejection: %s\n' "$name" >&2
        exit 1
    fi
}

cp "$ROOT_DIR/config/linux_defconfig" "$TMP_DIR/valid.config"
python3 "$VALIDATOR" "$TMP_DIR/valid.config" >/dev/null

python3 - "$TMP_DIR/valid.config" "$TMP_DIR/unknown.config" <<'PY'
from pathlib import Path
import sys
source, target = map(Path, sys.argv[1:])
target.write_text(
    source.read_text() + "\nCONFIG_ACTRUST_UNKNOWN_SYMBOL=y\n",
    encoding="utf-8",
)
PY
assert_rejected unknown-symbol "$TMP_DIR/unknown.config"

auto_invalid="$TMP_DIR/invalid-range.config"
sed 's/CONFIG_ACTRUST_CORE_JOB_POOL_SIZE=8/CONFIG_ACTRUST_CORE_JOB_POOL_SIZE=1/' \
    "$TMP_DIR/valid.config" >"$auto_invalid"
assert_rejected invalid-range "$auto_invalid"

python3 - "$TMP_DIR/valid.config" "$TMP_DIR/invalid-string.config" <<'PY'
from pathlib import Path
import sys
source, target = map(Path, sys.argv[1:])
text = source.read_text(encoding="utf-8")
text = text.replace(
    'CONFIG_ACTRUST_NTP_SERVER="pool.ntp.org"',
    'CONFIG_ACTRUST_NTP_SERVER="$(touch /tmp/should-not-run)"',
)
target.write_text(text, encoding="utf-8")
PY
assert_rejected invalid-string "$TMP_DIR/invalid-string.config"

python3 - "$TMP_DIR/valid.config" "$TMP_DIR/invalid-profile.config" <<'PY'
from pathlib import Path
import sys
source, target = map(Path, sys.argv[1:])
text = source.read_text(encoding="utf-8")
text = text.replace(
    'CONFIG_ACTRUST_COMPONENTS_JSON=y',
    '# CONFIG_ACTRUST_COMPONENTS_JSON is not set',
)
target.write_text(text, encoding="utf-8")
PY
assert_rejected invalid-profile "$TMP_DIR/invalid-profile.config"

printf 'sentinel\n' >"$TMP_DIR/actrust_config.h"
printf 'sentinel\n' >"$TMP_DIR/actrust_config.cmake"
if "$GENERATOR" "$TMP_DIR/unknown.config" "$TMP_DIR" >"$TMP_DIR/generator.out" 2>&1; then
    printf 'expected generator rejection\n' >&2
    exit 1
fi
[[ $(<"$TMP_DIR/actrust_config.h") == "sentinel" ]]
[[ $(<"$TMP_DIR/actrust_config.cmake") == "sentinel" ]]

printf 'Configuration validation tests passed.\n'
