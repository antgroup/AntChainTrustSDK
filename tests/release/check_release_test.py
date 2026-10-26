# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

"""Release-gate regressions using isolated, tracked public-header fixtures."""

from contextlib import redirect_stderr, redirect_stdout
from importlib.util import module_from_spec, spec_from_file_location
from io import StringIO
import os
from pathlib import Path
import re
import subprocess
import tempfile
import textwrap
import unittest


CHECKER = Path(__file__).resolve().parents[2] / "tools/release/check_release.py"
WORKFLOW = CHECKER.parents[2] / ".github/workflows/release.yml"
SPEC = spec_from_file_location("check_release", CHECKER)
release = module_from_spec(SPEC)
SPEC.loader.exec_module(release)

HEADER = """\
#ifndef TEST_H
#define TEST_H
typedef struct {
    int first;
    union {
        int number;
        void *pointer;
    } value;
    int last;
} actrust_public_t;
actrust_err_t actrust_probe(int value);
#endif
"""


def git(root, *args, **kwargs):
    return subprocess.run(
        ["git", "-C", str(root), *args], check=True, capture_output=True,
        text=True, **kwargs,
    ).stdout.strip()


def initialize(root, header=HEADER):
    root.mkdir()
    git(root, "init", "--quiet")
    git(root, "config", "user.email", "release-test@example.invalid")
    git(root, "config", "user.name", "Release Test")
    git(root, "config", "commit.gpgsign", "false")
    git(root, "config", "tag.gpgsign", "false")
    (root / "include").mkdir()
    (root / "include/actrust.h").write_text(header, encoding="utf-8")
    git(root, "add", "include/actrust.h")
    git(root, "commit", "--quiet", "-m", "Fixture baseline")


def metadata(root, version):
    (root / "VERSION").write_text(version + "\n", encoding="utf-8")
    (root / "CMakeLists.txt").write_text(
        "project(AntChainTrustSDK VERSION ${ACTRUST_VERSION})\n",
        encoding="utf-8",
    )
    (root / "Doxyfile").write_text(
        f"PROJECT_NUMBER = {version}\nOUTPUT_DIRECTORY = docs/v{version}\n",
        encoding="utf-8",
    )
    (root / "CHANGELOG.md").write_text(
        f"## [{version}]\n", encoding="utf-8",
    )
    notes = root / "docs/releases"
    notes.mkdir(parents=True, exist_ok=True)
    (notes / f"v{version}.md").write_text("Release notes\n", encoding="utf-8")


class ReleaseChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.old = self.root / "old"
        self.new = self.root / "new"
        initialize(self.old)
        initialize(self.new)

    def assert_failure(self, diagnostic, callback, *args):
        errors = StringIO()
        with redirect_stderr(errors), self.assertRaises(SystemExit) as failure:
            callback(*args)
        self.assertEqual(failure.exception.code, 1)
        self.assertIn(diagnostic, errors.getvalue())

    def check_api_and_abi(self):
        with redirect_stdout(StringIO()):
            release.check_api(self.new, "baseline")
            release.check_abi_headers(self.old, self.new)

    def test_multiline_struct_is_a_complete_declaration(self):
        self.assertEqual(release.api_lines(HEADER), [
            "typedef struct { int first; union { int number; void *pointer; "
            "} value; int last; } actrust_public_t;",
            "actrust_err_t actrust_probe(int value);",
        ])

    def test_public_declaration_forms(self):
        source = """\
#define ACTRUST_PUBLIC_FLAG 1
typedef struct actrust_ctx *actrust_t;
typedef void (*actrust_callback_t)(
    int result, void *user_data);
typedef enum {
    ACTRUST_OPEN = '{',
    ACTRUST_CLOSE = '}',
} actrust_enum_t;
typedef union {
    int number;
    void *pointer;
} actrust_union_t;
"""
        self.assertEqual(release.api_lines(source), [
            "#define ACTRUST_PUBLIC_FLAG 1",
            "typedef struct actrust_ctx *actrust_t;",
            "typedef void (*actrust_callback_t)( int result, void *user_data);",
            "typedef enum { ACTRUST_OPEN = '{', ACTRUST_CLOSE = '}', "
            "} actrust_enum_t;",
            "typedef union { int number; void *pointer; } actrust_union_t;",
        ])

    def test_unchanged_and_additive_headers_pass(self):
        git(self.new, "tag", "baseline")
        self.check_api_and_abi()
        (self.new / "include/actrust.h").write_text(
            HEADER + "actrust_err_t actrust_added(void);\n", encoding="utf-8",
        )
        self.check_api_and_abi()

    def test_incompatible_headers_report_compatibility_failure(self):
        git(self.new, "tag", "baseline")
        changes = {
            "nested member type": HEADER.replace(
                "void *pointer", "int pointer",
            ),
            "later member type": HEADER.replace("int last", "long last"),
            "later member removed": HEADER.replace("    int last;\n", ""),
            "member appended": HEADER.replace(
                "    int last;", "    int last;\n    int added;",
            ),
            "member order": HEADER.replace("int first", "int last").replace(
                "    int last;\n}", "    int first;\n}",
            ),
            "typedef name": HEADER.replace(
                "actrust_public_t", "actrust_other_t",
            ),
            "signature": HEADER.replace(
                "actrust_probe(int", "actrust_probe(long",
            ),
        }
        for name, changed in changes.items():
            with self.subTest(change=name):
                (self.new / "include/actrust.h").write_text(
                    changed, encoding="utf-8",
                )
                self.assert_failure(
                    "incompatible public API changes:",
                    release.check_api, self.new, "baseline",
                )
                self.assert_failure(
                    "public ABI declarations changed:",
                    release.check_abi_headers, self.old, self.new,
                )

    def test_removed_header_is_rejected(self):
        git(self.new, "tag", "baseline")
        git(self.new, "rm", "include/actrust.h")
        self.assert_failure(
            "incompatible public API changes:",
            release.check_api, self.new, "baseline",
        )
        self.assert_failure(
            "public ABI declarations changed:",
            release.check_abi_headers, self.old, self.new,
        )

    def test_additive_enum_and_count_at_end_of_header(self):
        cases = [
            (
                "typedef enum { ACTRUST_OLD = 1, } actrust_enum_t;\n",
                "typedef enum { ACTRUST_OLD = 1, ACTRUST_NEW = 2, } "
                "actrust_enum_t;\n",
            ),
            (
                "#define ACTRUST_ITEM_COUNT 1\n",
                "#define ACTRUST_ITEM_COUNT 2\n",
            ),
        ]
        for old, new in cases:
            with self.subTest(declaration=old):
                for root in (self.old, self.new):
                    (root / "include/actrust.h").write_text(
                        old, encoding="utf-8",
                    )
                git(self.new, "add", "include/actrust.h")
                git(self.new, "commit", "--quiet", "-m", "Extension baseline")
                git(self.new, "tag", "--force", "baseline")
                (self.new / "include/actrust.h").write_text(
                    new, encoding="utf-8",
                )
                self.check_api_and_abi()

    def test_enum_value_changes_are_rejected(self):
        old = "typedef enum { ACTRUST_OLD = 1, } actrust_enum_t;\n"
        new = old.replace("= 1", "= 2")
        for root in (self.old, self.new):
            (root / "include/actrust.h").write_text(old, encoding="utf-8")
        git(self.new, "add", "include/actrust.h")
        git(self.new, "commit", "--quiet", "-m", "Enum baseline")
        git(self.new, "tag", "baseline")
        (self.new / "include/actrust.h").write_text(new, encoding="utf-8")
        self.assert_failure(
            "incompatible public API changes:",
            release.check_api, self.new, "baseline",
        )
        self.assert_failure(
            "public ABI declarations changed:",
            release.check_abi_headers, self.old, self.new,
        )

    def test_latest_stable_ancestor_uses_numeric_version_order(self):
        for tag in ("v1.9.0", "v1.11.0-rc.1", "v9.0.0", "v1x10x9"):
            git(self.new, "tag", tag)
        git(self.new, "tag", "-a", "v1.10.0", "-m", "Annotated baseline")
        git(self.new, "commit", "--allow-empty", "--quiet", "-m", "Release")
        git(self.new, "tag", "v1.11.0")
        unrelated = git(self.new, "commit-tree", "HEAD^{tree}", input="Other\n")
        git(self.new, "tag", "v1.10.99", unrelated)
        self.assertEqual(
            release.find_baseline_tag(self.new, "v1.11.0"), "v1.10.0",
        )
        result = subprocess.run(
            ["python3", str(CHECKER), "--root", str(self.new),
             "--release-tag", "v1.11.0", "--print-baseline-tag"],
            check=True, capture_output=True, text=True,
        )
        self.assertEqual(result.stdout, "v1.10.0\n")

    def test_missing_baseline_has_a_specific_error(self):
        git(self.new, "tag", "v2.0.0")
        self.assert_failure(
            "no earlier stable release tag is an ancestor of refs/tags/v2.0.0",
            release.find_baseline_tag, self.new, "v2.0.0",
        )

    def test_metadata_and_tag_checks_survive_version_bumps(self):
        git(self.new, "tag", "v2.7.2")
        for version in ("2.7.3", "4.0.0"):
            with self.subTest(version=version):
                metadata(self.new, version)
                with redirect_stdout(StringIO()):
                    release.check_release(self.new, version, f"v{version}")
                self.assert_failure(
                    f"does not match v{version}", release.check_release,
                    self.new, version, f"v{version}-rc.1",
                )
                result = subprocess.run(
                    ["python3", str(CHECKER), "--root", str(self.new),
                     "--release-tag", f"v{version}", "--print-baseline-tag",
                     "--baseline-ref", "HEAD"],
                    check=True, capture_output=True, text=True,
                )
                self.assertEqual(result.stdout, "v2.7.2\n")

    def test_missing_metadata_has_a_specific_error(self):
        (self.new / "VERSION").write_text("2.0.0\n", encoding="utf-8")
        self.assert_failure(
            "cannot read release metadata:", release.check_release,
            self.new, "2.0.0", None,
        )


class HistoricalReleaseWorkflowChecks(unittest.TestCase):
    """Run workflow gates with newer tooling and a detached historical tag."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.workspace = self.root / "workspace"
        initialize(self.workspace)
        metadata(self.workspace, "1.0.0")
        checker = self.workspace / "tools/release/check_release.py"
        checker.parent.mkdir(parents=True)
        checker.write_text(
            "import argparse\n"
            "parser = argparse.ArgumentParser()\n"
            "parser.add_argument('--release-tag')\n"
            "parser.parse_args()\n",
            encoding="utf-8",
        )
        build_script = self.workspace / "build.sh"
        build_script.write_text(
            "#!/usr/bin/env bash\n"
            "set -euo pipefail\n"
            "test \"${1}\" = linux_x86\n"
            "test \"${2}\" = --clean-build\n"
            "mkdir -p build/config\n"
            "echo configuration > build/config/actrust_config.cmake\n"
            "test -s \"${BUILD_DIR}/config/actrust_config.cmake\"\n"
            "echo baseline > \"${BUILD_DIR}/baseline-built\"\n",
            encoding="utf-8",
        )
        build_script.chmod(0o755)
        git(self.workspace, "add", ".")
        git(self.workspace, "commit", "--quiet", "-m", "Old release tooling")
        git(self.workspace, "tag", "v1.0.0")
        metadata(self.workspace, "1.1.0")
        git(self.workspace, "add", ".")
        git(self.workspace, "commit", "--quiet", "-m", "Historical release")
        git(self.workspace, "tag", "v1.1.0")
        metadata(self.workspace, "9.0.0")
        checker.write_text(CHECKER.read_text(encoding="utf-8"),
                           encoding="utf-8")
        git(self.workspace, "add", ".")
        git(self.workspace, "commit", "--quiet", "-m", "Current release tools")
        self.source = self.workspace / "release-source"
        git(self.workspace, "worktree", "add", "--detach", str(self.source),
            "v1.1.0")
        self.runner_temp = self.root / "runner-temp"
        self.runner_temp.mkdir()
        self.github_env = self.root / "github-env"
        self.env = dict(os.environ, GITHUB_WORKSPACE=str(self.workspace),
                        RUNNER_TEMP=str(self.runner_temp),
                        GITHUB_ENV=str(self.github_env), RELEASE_TAG="v1.1.0")

    def run_step(self, name):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        step = re.search(
            rf"(?ms)^      - name: {re.escape(name)}\n"
            r"(.*?)(?=^      - name: |\Z)", workflow,
        )
        self.assertIsNotNone(step, f"workflow step missing: {name}")
        script = re.search(r"(?ms)^        run: \|\n(.*)", step.group(1))
        self.assertIsNotNone(script, f"workflow run block missing: {name}")
        directory = re.search(
            r"(?m)^    defaults:\n      run:\n"
            r"        working-directory: (.+)$", workflow,
        )
        cwd = self.workspace
        if directory:
            cwd /= directory.group(1)
        return subprocess.run(
            ["bash", "--noprofile", "--norc", "-e", "-o", "pipefail", "-c",
             textwrap.dedent(script.group(1))],
            cwd=cwd, env=self.env, capture_output=True, text=True, check=False,
        )

    def test_historical_tag_uses_current_checker_and_its_own_version(self):
        legacy = subprocess.run(
            ["python3", "tools/release/check_release.py", "--release-tag",
             "v1.1.0", "--print-baseline-tag"], cwd=self.source,
            capture_output=True, text=True, check=False,
        )
        self.assertEqual(legacy.returncode, 2)
        self.assertIn("unrecognized arguments: --print-baseline-tag",
                      legacy.stderr)
        result = self.run_step("Validate release metadata and public API")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Release metadata: PASS (1.1.0)", result.stdout)
        self.assertIn("API diff: PASS", result.stdout)
        self.assertEqual(self.github_env.read_text(), "BASELINE_TAG=v1.0.0\n")

    def test_historical_metadata_failure_stops_workflow(self):
        (self.source / "Doxyfile").write_text(
            "PROJECT_NUMBER = 9.0.0\nOUTPUT_DIRECTORY = docs/v9.0.0\n",
            encoding="utf-8",
        )
        result = self.run_step("Validate release metadata and public API")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Doxyfile PROJECT_NUMBER does not match VERSION",
                      result.stderr)

    def test_historical_public_api_failure_stops_workflow(self):
        (self.source / "include/actrust.h").write_text(
            HEADER.replace("int last", "long last"), encoding="utf-8",
        )
        result = self.run_step("Validate release metadata and public API")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("incompatible public API changes:", result.stderr)

    def test_baseline_build_uses_historical_configuration_directory(self):
        release_build = self.runner_temp / "actrust-release"
        release_build.mkdir()
        self.env.update(BASELINE_TAG="v1.0.0", BUILD_DIR=str(release_build))
        result = self.run_step("Build ABI baseline")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        baseline_build = self.runner_temp / "actrust-baseline/build"
        self.assertEqual(self.github_env.read_text(),
                         f"ABI_BASELINE_DIR={baseline_build}\n")
        self.assertEqual((baseline_build / "baseline-built").read_text(),
                         "baseline\n")
        self.assertTrue(
            (baseline_build / "config/actrust_config.cmake").is_file(),
        )
        self.assertEqual(list(release_build.iterdir()), [])

    def test_bundle_gate_checks_historical_abi_headers_and_archives(self):
        baseline = self.runner_temp / "actrust-baseline"
        git(self.workspace, "worktree", "add", "--detach", str(baseline),
            "v1.0.0")
        build = self.runner_temp / "actrust-release"
        baseline_build = baseline / "build"
        fixture = self.root / "bundle-fixture"
        fixture.mkdir()
        (fixture / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.16)\n"
            "project(BundleFixture NONE)\n"
            "add_custom_target(actrust_sdk_bundle)\n",
            encoding="utf-8",
        )
        subprocess.run(["cmake", "-S", str(fixture), "-B", str(build)],
                       check=True, capture_output=True, text=True)
        for directory in (baseline_build, build):
            directory.mkdir(exist_ok=True)
            obj = directory / "probe.o"
            subprocess.run(
                ["cc", "-x", "c", "-c", "-o", str(obj), "-"],
                input="int actrust_probe(int value) { return value; }\n",
                check=True, capture_output=True, text=True,
            )
            subprocess.run(
                ["ar", "rcs", str(directory / "libactrust.a"), str(obj)],
                check=True, capture_output=True, text=True,
            )
        for path in ("AntChainTrustSDK-1.1.0-linux_x86.tar.gz",
                     "AntChainTrustSDK-1.1.0-linux_x86.tar.gz.sha256",
                     "bundle-metadata/sbom.spdx.json"):
            output = build / path
            output.parent.mkdir(exist_ok=True)
            output.write_text("fixture\n", encoding="utf-8")
        self.env.update(BUILD_DIR=str(build),
                        ABI_BASELINE_DIR=str(baseline_build))
        result = self.run_step("Verify bundle metadata and checksum")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Release metadata: PASS (1.1.0)", result.stdout)
        self.assertIn("ABI diff: PASS (1 baseline symbols, 1 current symbols)",
                      result.stdout)
        (self.source / "include/actrust.h").write_text(
            HEADER.replace("int last", "long last"), encoding="utf-8",
        )
        result = self.run_step("Verify bundle metadata and checksum")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("public ABI declarations changed:", result.stderr)
        (self.source / "include/actrust.h").write_text(HEADER, encoding="utf-8")
        subprocess.run(["ar", "d", str(build / "libactrust.a"), "probe.o"],
                       check=True, capture_output=True, text=True)
        result = self.run_step("Verify bundle metadata and checksum")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ABI symbols removed: actrust_probe", result.stderr)


if __name__ == "__main__":
    unittest.main()
