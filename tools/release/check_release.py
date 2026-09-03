#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

"""Offline release gates for AntChainTrustSDK."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path
from typing import Optional

SEMVER = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")


def fail(message: str) -> None:
    print(f"release check failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def git(root: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(root), *args],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        fail(result.stderr.strip() or "git command failed")
    return result.stdout


def read_version(root: Path) -> str:
    try:
        version = (root / "VERSION").read_text(encoding="utf-8").strip()
    except OSError as exc:
        fail(f"cannot read VERSION: {exc}")
    if not SEMVER.fullmatch(version):
        fail(f"VERSION is not semantic version: {version!r}")
    return version


def is_public_header(path: str) -> bool:
    if path.startswith("source/core/include/"):
        return False
    return (
        path.startswith("include/") or path.startswith("source/adapter/include/")
        or "/components/" in path and "/include/" in path
    ) and path.endswith(".h") and not path.endswith("_internal.h") and "/tests/" not in f"/{path}"


def header_paths(root: Path) -> list[str]:
    return sorted(path for path in git(root, "ls-files").splitlines() if is_public_header(path))


def baseline_header_paths(root: Path, tag: str) -> list[str]:
    files = git(root, "ls-tree", "-r", "--name-only", tag).splitlines()
    return sorted(path for path in files if is_public_header(path))


def api_lines(text: str) -> list[str]:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"//.*", "", text)
    lines = []
    index = 0
    source_lines = text.splitlines()
    while index < len(source_lines):
        line = source_lines[index].strip()
        index += 1
        if not line:
            continue
        if line.startswith("#define ACTRUST_"):
            record = [line]
            while record[-1].endswith("\\") and index < len(source_lines):
                record.append(source_lines[index].strip())
                index += 1
            lines.append(re.sub(r"\s+", " ", " ".join(record)))
            continue
        if line.startswith("#"):
            continue
        record = [line]
        starts_type = line.startswith("typedef struct") or line.startswith("typedef enum")
        starts_decl = starts_type or line.startswith("typedef ") or (
            "actrust_" in line and ("(" in line or line.startswith("extern "))
        )
        if not starts_decl:
            continue
        while not record[-1].endswith(";") and index < len(source_lines):
            record.append(source_lines[index].strip())
            index += 1
        normalized = re.sub(r"\s+", " ", " ".join(record))
        lines.append(normalized)
    return lines


def current_api(root: Path) -> dict[str, list[str]]:
    result = {}
    for path in header_paths(root):
        try:
            result[path] = api_lines((root / path).read_text(encoding="utf-8"))
        except OSError as exc:
            fail(f"cannot read public header {path}: {exc}")
    return result


def baseline_api(root: Path, tag: str) -> dict[str, list[str]]:
    result = {}
    for path in baseline_header_paths(root, tag):
        result[path] = api_lines(git(root, "show", f"{tag}:{path}"))
    return result


def compatible_api_replacement(old: str, new: str) -> bool:
    pattern = re.compile(r"#define (ACTRUST_[A-Z0-9_]*_COUNT) ([0-9]+)")
    old_match = pattern.fullmatch(old)
    new_match = pattern.fullmatch(new)
    return bool(
        old_match
        and new_match
        and old_match.group(1) == new_match.group(1)
        and int(new_match.group(2)) == int(old_match.group(2)) + 1
    )


def is_compatible_enum_extension(old: str, new: str) -> bool:
    if not old.startswith("typedef enum") or not new.startswith("typedef enum"):
        return False
    old_name = old.rsplit("}", 1)[-1]
    new_name = new.rsplit("}", 1)[-1]
    if old_name != new_name or not new.startswith(old.split("}", 1)[0]):
        return False
    return all(member in new for member in re.findall(r"ACTRUST_[A-Z0-9_]+", old))


def is_compatible_count_change(old: str, new: str) -> bool:
    return compatible_api_replacement(old, new) or is_compatible_enum_extension(old, new)


def check_api(root: Path, tag: str) -> None:
    current = current_api(root)
    baseline = baseline_api(root, tag)
    removed = []
    added = []
    for path in sorted(set(current) | set(baseline)):
        old_lines = baseline.get(path, [])
        new_lines = current.get(path, [])
        old_index = 0
        for line in new_lines:
            if old_index < len(old_lines) and line == old_lines[old_index]:
                old_index += 1
            elif old_index < len(old_lines) and is_compatible_count_change(
                old_lines[old_index], line
            ):
                old_index += 1
            else:
                added.append((path, line))
        if old_index != len(old_lines):
            removed.extend((path, line) for line in old_lines[old_index:])
    if removed:
        details = ", ".join(f"{path}: {line}" for path, line in removed)
        fail("incompatible public API changes: " + details)
    print(f"API diff: PASS ({len(added)} additive public declarations)")
    for path, line in added:
        print(f"  {path}: {line}")


def archive_paths(path: Path) -> list[Path]:
    if path.is_file():
        return [path]
    if path.is_dir():
        archives = sorted(path.rglob("*.a"))
        if not archives:
            fail(f"no static archives found under {path}")
        return archives
    fail(f"ABI input does not exist: {path}")
    return []


def symbols(path: Path) -> set[str]:
    found = set()
    for archive in archive_paths(path):
        result = subprocess.run(
            ["nm", "-g", "--defined-only", str(archive)],
            check=False,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            fail(result.stderr.strip() or f"cannot inspect ABI archive {archive}")
        for line in result.stdout.splitlines():
            fields = line.split()
            if fields and fields[-1].startswith("actrust_"):
                found.add(fields[-1])
    return found


def public_header_snapshot(root: Path) -> dict[str, list[str]]:
    records = {}
    for path in header_paths(root):
        try:
            records[path] = api_lines((root / path).read_text(encoding="utf-8"))
        except OSError as exc:
            fail(f"cannot read public header {path}: {exc}")
    return records


def check_abi_headers(baseline: Path, current: Path) -> None:
    old = public_header_snapshot(baseline)
    new = public_header_snapshot(current)
    changed = []
    for path, old_lines in old.items():
        new_lines = new.get(path, [])
        new_index = 0
        for line in old_lines:
            while new_index < len(new_lines) and new_lines[new_index] != line:
                if new_index + 1 < len(new_lines) and is_compatible_count_change(
                    line, new_lines[new_index]
                ):
                    break
                new_index += 1
            if new_index == len(new_lines):
                changed.append(f"{path}: {line}")
                continue
            if new_lines[new_index] != line and not is_compatible_count_change(
                line, new_lines[new_index]
            ):
                changed.append(f"{path}: {line}")
            new_index += 1
    if changed:
        fail("public ABI declarations changed: " + "; ".join(changed))


def check_abi(
    baseline: Path,
    current: Path,
    baseline_headers: Optional[Path],
    current_headers: Optional[Path],
) -> None:
    old = symbols(baseline)
    new = symbols(current)
    removed = sorted(old - new)
    if removed:
        fail("ABI symbols removed: " + ", ".join(removed))
    if (baseline_headers is None) != (current_headers is None):
        fail("ABI header roots must be provided together")
    if baseline_headers is not None:
        check_abi_headers(baseline_headers, current_headers)
    print(f"ABI diff: PASS ({len(old)} baseline symbols, {len(new)} current symbols)")


def check_release(root: Path, version: str, release_tag: Optional[str]) -> None:
    actual = read_version(root)
    if actual != version:
        fail(f"VERSION={actual} does not match requested {version}")
    if release_tag is not None and release_tag != f"v{version}":
        fail(f"release tag {release_tag} does not match v{version}")
    try:
        cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
        doxy = (root / "Doxyfile").read_text(encoding="utf-8")
        changelog = (root / "CHANGELOG.md").read_text(encoding="utf-8")
    except OSError as exc:
        fail(f"cannot read release metadata: {exc}")
    if "VERSION ${ACTRUST_VERSION}" not in cmake:
        fail("CMakeLists.txt does not consume ACTRUST_VERSION")
    doxy_number = re.search(r"^PROJECT_NUMBER\s*=\s*(\S+)", doxy, re.MULTILINE)
    doxy_output = re.search(r"^OUTPUT_DIRECTORY\s*=\s*(\S+)", doxy, re.MULTILINE)
    if doxy_number is None or doxy_number.group(1) != version:
        fail("Doxyfile PROJECT_NUMBER does not match VERSION")
    if doxy_output is None or doxy_output.group(1) != f"docs/v{version}":
        fail("Doxyfile OUTPUT_DIRECTORY does not match VERSION")
    if f"## [{version}]" not in changelog:
        fail("CHANGELOG.md has no release entry")
    notes = root / "docs" / "releases" / f"v{version}.md"
    if not notes.is_file():
        fail(f"release notes missing: {notes}")
    print(f"Release metadata: PASS ({version})")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root", type=Path, default=Path(__file__).resolve().parents[2]
    )
    parser.add_argument("--version")
    parser.add_argument("--release-tag")
    parser.add_argument("--baseline-tag")
    parser.add_argument("--api", action="store_true")
    parser.add_argument("--abi-baseline", type=Path)
    parser.add_argument("--abi-current", type=Path)
    parser.add_argument("--abi-header-baseline", type=Path)
    parser.add_argument("--abi-header-current", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    version = args.version or read_version(root)
    check_release(root, version, args.release_tag)
    if args.api:
        if not args.baseline_tag:
            fail("--baseline-tag is required with --api")
        git(root, "rev-parse", "--verify", args.baseline_tag)
        check_api(root, args.baseline_tag)
    if (args.abi_baseline is None) != (args.abi_current is None):
        fail("--abi-baseline and --abi-current must be provided together")
    if (args.abi_header_baseline is None) != (args.abi_header_current is None):
        fail("--abi-header-baseline and --abi-header-current must be provided together")
    if args.abi_baseline is not None:
        check_abi(
            args.abi_baseline,
            args.abi_current,
            args.abi_header_baseline,
            args.abi_header_current,
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
