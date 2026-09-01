#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG_FILE="${1:-$ROOT_DIR/.config}"
OUT_DIR="${2:-$ROOT_DIR/build/config}"
OUT_FILE="$OUT_DIR/actrust_config.h"
CMAKE_FILE="$OUT_DIR/actrust_config.cmake"

if [[ ! -f $CONFIG_FILE ]]; then
    echo "Config file not found: $CONFIG_FILE" >&2
    exit 1
fi

if ! python3 "$ROOT_DIR/tools/config_validate.py" "$CONFIG_FILE"; then
    echo "Configuration generation aborted; existing generated files were preserved" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

echo "Begin generating $OUT_FILE from $CONFIG_FILE"
echo "Begin generating $CMAKE_FILE from $CONFIG_FILE"

TMP_OUT_FILE="$(mktemp "$OUT_FILE.tmp.XXXXXX")"
TMP_CMAKE_FILE="$(mktemp "$CMAKE_FILE.tmp.XXXXXX")"
cleanup() {
    rm -f "$TMP_OUT_FILE" "$TMP_CMAKE_FILE"
}
trap cleanup EXIT

awk -v SRC="$CONFIG_FILE" '
function ltrim(s) { sub(/^[ \t\r\n]+/, "", s); return s }
function rtrim(s) { sub(/[ \t\r\n]+$/, "", s); return s }
function trim(s)  { return rtrim(ltrim(s)) }
function normalize_escaped_string(s) {
    gsub(/\\\\n/, "\\n", s);
    gsub(/\\\\r/, "\\r", s);
    gsub(/\\\\t/, "\\t", s);
    return s;
}

BEGIN {
    print "/* Auto-generated from .config. Do not edit. */";
    print "/* Source: " SRC " */";
    print "#pragma once";
    print "";
}

# Handle: # CONFIG_FOO is not set
/^# CONFIG_[A-Za-z0-9_]+ is not set[ \t]*$/ {
    key = $2;
    print "/* #undef " key " */";
    next;
}

# Handle: CONFIG_FOO=...
/^CONFIG_[A-Za-z0-9_]+=.*/ {
    split($0, parts, "=");
    key = parts[1];
    val = substr($0, length(key) + 2);
    val = trim(val);
    sub(/[ \t]+#.*$/, "", val);
    val = trim(val);
    if (val == "y") { print "#define " key " 1"; next; }
    if (val == "m") { print "#define " key " 1"; next; }
    if (val == "n" || val == "") { print "/* #undef " key " */"; next; }
    if (val ~ /^".*"$/) {
        val = normalize_escaped_string(val);
        print "#define " key " " val;
        next;
    }
    if (val ~ /^-?[0-9]+$/ || val ~ /^0x[0-9a-fA-F]+$/) {
        print "#define " key " " val; next;
    }
    print "#define " key " " val;
    next;
}
{ next; }
' "$CONFIG_FILE" >"$TMP_OUT_FILE"

awk -v SRC="$CONFIG_FILE" '
function ltrim(s) { sub(/^[ \t\r\n]+/, "", s); return s }
function rtrim(s) { sub(/[ \t\r\n]+$/, "", s); return s }
function trim(s)  { return rtrim(ltrim(s)) }
function normalize_escaped_string(s) {
    gsub(/\\\\n/, "\\n", s);
    gsub(/\\\\r/, "\\r", s);
    gsub(/\\\\t/, "\\t", s);
    return s;
}

BEGIN {
    print "# Auto-generated from .config. Do not edit.";
    print "# Source: " SRC;
    print "";
}

/^# CONFIG_[A-Za-z0-9_]+ is not set[ \t]*$/ {
    key = $2;
    print "set(" key " OFF)";
    next;
}

/^CONFIG_[A-Za-z0-9_]+=.*/ {
    split($0, parts, "=");
    key = parts[1];
    val = substr($0, length(key) + 2);
    val = trim(val);
    sub(/[ \t]+#.*$/, "", val);
    val = trim(val);
    if (val == "y" || val == "m") { print "set(" key " ON)"; next; }
    if (val == "n" || val == "") { print "set(" key " OFF)"; next; }
    if (val ~ /^".*"$/) {
        val = normalize_escaped_string(val);
        print "set(" key " " val ")";
        next;
    }
    if (val ~ /^-?[0-9]+$/ || val ~ /^0x[0-9a-fA-F]+$/) {
        print "set(" key " " val ")"; next;
    }
    print "set(" key " \"" val "\")";
    next;
}
{ next; }
' "$CONFIG_FILE" >"$TMP_CMAKE_FILE"

if [[ ! -f $OUT_FILE ]] || ! cmp -s "$TMP_OUT_FILE" "$OUT_FILE"; then
    mv "$TMP_OUT_FILE" "$OUT_FILE"
    echo "Generated $OUT_FILE from $CONFIG_FILE"
else
    echo "$OUT_FILE unchanged"
fi

if [[ ! -f $CMAKE_FILE ]] || ! cmp -s "$TMP_CMAKE_FILE" "$CMAKE_FILE"; then
    mv "$TMP_CMAKE_FILE" "$CMAKE_FILE"
    echo "Generated $CMAKE_FILE from $CONFIG_FILE"
else
    echo "$CMAKE_FILE unchanged"
fi
