#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

python3 "$ROOT_DIR/tools/release/check_release.py" --root "$ROOT_DIR"

printf '2.0.0\n' >"$TMP_DIR/VERSION"
if python3 "$ROOT_DIR/tools/release/check_release.py" --root "$TMP_DIR" >"$TMP_DIR/out" 2>&1; then
    printf 'expected invalid release root to fail\n' >&2
    exit 1
fi
if ! grep -q 'cannot read release metadata' "$TMP_DIR/out"; then
    printf 'unexpected invalid-root error\n' >&2
    cat "$TMP_DIR/out" >&2
    exit 1
fi

if python3 "$ROOT_DIR/tools/release/check_release.py" \
    --root "$ROOT_DIR" --release-tag v1.0.1 >"$TMP_DIR/tag-out" 2>&1; then
    printf 'expected mismatched release tag to fail\n' >&2
    exit 1
fi
if ! grep -q 'does not match v1.1.0' "$TMP_DIR/tag-out"; then
    printf 'unexpected tag mismatch error\n' >&2
    sed -n '1,20p' "$TMP_DIR/tag-out" >&2
    exit 1
fi

if python3 "$ROOT_DIR/tools/release/check_release.py" \
    --root "$ROOT_DIR" --api --baseline-tag v1.0.0 >"$TMP_DIR/api-out" 2>&1; then
    :
else
    printf 'expected additive API changes to pass\n' >&2
    sed -n '1,40p' "$TMP_DIR/api-out" >&2
    exit 1
fi

if python3 - "$ROOT_DIR/tools/release/check_release.py" <<'PY'
from importlib.util import module_from_spec, spec_from_file_location
import sys

spec = spec_from_file_location("check_release", sys.argv[1])
module = module_from_spec(spec)
spec.loader.exec_module(module)

assert module.api_lines("#define ACTRUST_PUBLIC_FLAG 1\n") == [
    "#define ACTRUST_PUBLIC_FLAG 1"
]
assert module.api_lines("typedef enum { ACTRUST_PUBLIC_OLD = 1, } actrust_public_t;\n")
assert module.api_lines(
    "typedef struct { int public_old_member; } actrust_public_struct_t;\n"
)
PY
then
    :
else
    printf 'API extraction regression test failed\n' >&2
    exit 1
fi

if python3 - "$ROOT_DIR/tools/release/check_release.py" "$TMP_DIR" <<'PY'
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import sys

spec = spec_from_file_location("check_release", sys.argv[1])
module = module_from_spec(spec)
spec.loader.exec_module(module)
root = Path(sys.argv[2])
old = root / "old" / "include"
new = root / "new" / "include"
old.mkdir(parents=True)
new.mkdir(parents=True)
header = """\
#ifndef TEST_H
#define TEST_H
typedef struct { int public_old_member; } actrust_public_struct_t;
actrust_err_t actrust_probe(int value);
#endif
"""
changed = header.replace("int public_old_member", "long public_old_member").replace(
    "actrust_probe(int value)", "actrust_probe(long value)"
)
(old / "actrust.h").write_text(header, encoding="utf-8")
(new / "actrust.h").write_text(changed, encoding="utf-8")
try:
    module.check_abi_headers(root / "old", root / "new")
except SystemExit:
    pass
else:
    raise AssertionError("ABI header change was not rejected")
PY
then
    :
else
    printf 'ABI header regression test failed\n' >&2
    exit 1
fi

printf 'Release check tests passed.\n'
