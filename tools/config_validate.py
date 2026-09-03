#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

"""Validate AntChainTrustSDK .config files before generating build files."""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple


ROOT_DIR = Path(__file__).resolve().parent.parent
KCONFIG_FILES = [
    ROOT_DIR / "3rdparts/Kconfig",
    ROOT_DIR / "source/adapter/Kconfig",
    ROOT_DIR / "source/components/Kconfig",
    ROOT_DIR / "source/components/cloud/Kconfig",
    ROOT_DIR / "source/components/crypto/Kconfig",
    ROOT_DIR / "source/components/json/Kconfig",
    ROOT_DIR / "source/components/kv/Kconfig",
    ROOT_DIR / "source/components/log/Kconfig",
    ROOT_DIR / "source/components/mqtt/Kconfig",
    ROOT_DIR / "source/components/ntp/Kconfig",
    ROOT_DIR / "source/components/queue/Kconfig",
    ROOT_DIR / "source/components/tls/Kconfig",
    ROOT_DIR / "source/core/Kconfig",
]

CONFIG_NAME = re.compile(r"^CONFIG_[A-Z0-9_]+$")
ASSIGNMENT = re.compile(r"^(CONFIG_[A-Z0-9_]+)=(.*)$")
NOT_SET = re.compile(r"^# (CONFIG_[A-Z0-9_]+) is not set$")
INTEGER = re.compile(r"^-?[0-9]+$")
HEXADECIMAL = re.compile(r"^0[xX][0-9a-fA-F]+$")
STRING = re.compile(r'^"([^"\\]|\\.)*"$')
SAFE_STRING = re.compile(r"^[A-Za-z0-9_./:@+\-]*$")


@dataclass
class Symbol:
    name: str
    kind: str
    ranges: List[Tuple[int, int]] = field(default_factory=list)


@dataclass
class Schema:
    symbols: Dict[str, Symbol] = field(default_factory=dict)
    choices: List[Tuple[str, List[str]]] = field(default_factory=list)


def parse_schema() -> Schema:
    schema = Schema()
    for path in KCONFIG_FILES:
        text = path.read_text(encoding="utf-8").splitlines()
        choice_name: Optional[str] = None
        choice_members: List[str] = []
        current: Optional[Symbol] = None
        for line in text:
            match = re.match(r"^\s*choice(?:\s+([A-Z0-9_]+))?\s*$", line)
            if match:
                choice_name = match.group(1) or f"choice@{path}:{len(schema.choices)}"
                choice_members = []
                current = None
                continue
            if re.match(r"^\s*endchoice\s*$", line):
                if choice_name is not None:
                    schema.choices.append((choice_name, choice_members))
                choice_name = None
                choice_members = []
                current = None
                continue
            match = re.match(r"^\s*(?:menuconfig|config)\s+([A-Z0-9_]+)\s*$", line)
            if match:
                name = f"CONFIG_{match.group(1)}"
                current = schema.symbols.get(name)
                if current is None:
                    current = Symbol(name=name, kind="unknown")
                    schema.symbols[name] = current
                if choice_name is not None and name not in choice_members:
                    choice_members.append(name)
                continue
            if current is None:
                continue
            match = re.match(r"^\s+(bool|int|hex|string)(?:\s|$)", line)
            if match:
                kind = match.group(1)
                if current.kind not in ("unknown", kind):
                    raise ValueError(f"symbol {current.name} has conflicting types")
                current.kind = kind
                continue
            match = re.match(r"^\s+range\s+(-?[0-9]+)\s+(-?[0-9]+)\s*$", line)
            if match:
                current.ranges.append((int(match.group(1)), int(match.group(2))))

    for symbol in schema.symbols.values():
        if symbol.kind == "unknown":
            raise ValueError(f"no type declared for {symbol.name}")
    return schema


def parse_value(config_path: Path) -> Tuple[Dict[str, str], List[str]]:
    values: Dict[str, str] = {}
    errors: List[str] = []
    for line_number, raw_line in enumerate(
        config_path.read_text(encoding="utf-8").splitlines(), 1
    ):
        line = raw_line.strip()
        if not line or line.startswith("#") and not NOT_SET.match(line):
            continue
        match = NOT_SET.match(line)
        if match:
            name = match.group(1)
            if name in values:
                errors.append(f"line {line_number}: duplicate symbol {name}")
            values[name] = "n"
            continue
        match = ASSIGNMENT.match(line)
        if not match:
            if line.startswith("CONFIG_") or line.startswith("# CONFIG_"):
                errors.append(f"line {line_number}: malformed configuration line")
            continue
        name, value = match.groups()
        if name in values:
            errors.append(f"line {line_number}: duplicate symbol {name}")
        values[name] = value.strip()
    return values, errors


def validate(config_path: Path) -> List[str]:
    schema = parse_schema()
    values, errors = parse_value(config_path)

    for name, value in values.items():
        symbol = schema.symbols.get(name)
        if symbol is None or not CONFIG_NAME.fullmatch(name):
            errors.append(f"unknown symbol: {name}")
            continue
        if symbol.kind == "bool":
            if value not in {"y", "n"}:
                errors.append(f"{name}: expected bool y/n, got {value!r}")
        elif symbol.kind == "int":
            if not INTEGER.fullmatch(value):
                errors.append(f"{name}: expected decimal integer, got {value!r}")
            else:
                number = int(value)
                for lower, upper in symbol.ranges:
                    if number < lower or number > upper:
                        errors.append(
                            f"{name}: value {number} outside range {lower}..{upper}"
                        )
        elif symbol.kind == "hex":
            if not HEXADECIMAL.fullmatch(value):
                errors.append(f"{name}: expected hexadecimal integer, got {value!r}")
        elif symbol.kind == "string":
            if not STRING.fullmatch(value):
                errors.append(f"{name}: expected a quoted string")
            else:
                decoded = value[1:-1]
                if not SAFE_STRING.fullmatch(decoded):
                    errors.append(
                        f"{name}: string contains unsafe CMake characters"
                    )

    def enabled(name: str) -> bool:
        return values.get(name) == "y"

    for choice_name, members in schema.choices:
        selected = [name for name in members if enabled(name)]
        if len(selected) != 1:
            errors.append(
                f"choice {choice_name}: expected exactly one selection, "
                f"got {', '.join(selected) or 'none'}"
            )

    platforms = [
        "CONFIG_ACTRUST_ADAPTER_PLATFORM_LINUX",
        "CONFIG_ACTRUST_ADAPTER_PLATFORM_ANDROID",
        "CONFIG_ACTRUST_ADAPTER_PLATFORM_SIMCOM_A7606E",
    ]
    selected_platforms = [name for name in platforms if enabled(name)]
    if len(selected_platforms) != 1:
        errors.append(
            "adapter platform: expected exactly one selection, "
            f"got {', '.join(selected_platforms) or 'none'}"
        )

    required_components = [
        "LOG",
        "JSON",
        "KV",
        "NTP",
        "QUEUE",
        "CRYPTO",
        "TLS",
        "MQTT",
        "CLOUD",
    ]
    for component in required_components:
        name = f"CONFIG_ACTRUST_COMPONENTS_{component}"
        if not enabled(name):
            errors.append(f"fixed Core profile requires {name}=y")

    required_backends = {
        "CONFIG_ACTRUST_3RDPARTS_CRYPTO_MBEDTLS": "Crypto/TLS requires mbedTLS",
        "CONFIG_ACTRUST_3RDPARTS_MQTT_COREMQTT": "MQTT requires coreMQTT + Agent",
        "CONFIG_ACTRUST_3RDPARTS_JSON_COREJSON": "JSON requires coreJSON",
        "CONFIG_ACTRUST_3RDPARTS_NTP_CORESNTP": "NTP requires coreSNTP",
        "CONFIG_ACTRUST_3RDPARTS_BACKOFF_BACKOFFALGO": "MQTT requires backoffAlgorithm",
    }
    for name, reason in required_backends.items():
        if not enabled(name):
            errors.append(f"{reason}: {name}=y is required")

    if enabled("CONFIG_ACTRUST_COMPONENTS_JSON") and not enabled(
        "CONFIG_ACTRUST_JSON_BACKEND_COREJSON"
    ):
        errors.append("JSON component requires CONFIG_ACTRUST_JSON_BACKEND_COREJSON=y")
    if enabled("CONFIG_ACTRUST_COMPONENTS_JSON") != enabled(
        "CONFIG_ACTRUST_3RDPARTS_JSON_COREJSON"
    ):
        errors.append("JSON component and third-party backend selections disagree")
    if enabled("CONFIG_ACTRUST_CRYPTO_KEY_PROFILE_PRODUCTION"):
        for name in (
            "CONFIG_ACTRUST_SEC_HW_KEY_MGMT",
            "CONFIG_ACTRUST_SEC_HW_ECDSA",
            "CONFIG_ACTRUST_SEC_HW_AES",
        ):
            if not enabled(name):
                errors.append(f"production key profile requires {name}=y")

    if enabled("CONFIG_ACTRUST_ADAPTER_PLATFORM_SIMCOM_A7606E") and (
        "CONFIG_ACTRUST_SEC_STORE_BASE_DIR" in values
    ):
        errors.append(
            "CONFIG_ACTRUST_SEC_STORE_BASE_DIR is not supported by SIMCom A7606E"
        )
    return errors


def main() -> int:
    if len(sys.argv) != 2:
        print(f"Usage: {Path(sys.argv[0]).name} CONFIG_FILE", file=sys.stderr)
        return 2
    config_path = Path(sys.argv[1])
    if not config_path.is_file():
        print(f"Config file not found: {config_path}", file=sys.stderr)
        return 2
    try:
        errors = validate(config_path)
    except (OSError, UnicodeError, ValueError) as error:
        print(f"Configuration schema error: {error}", file=sys.stderr)
        return 1
    if errors:
        print(f"Configuration validation failed: {config_path}", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print(f"Configuration validation passed: {config_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
