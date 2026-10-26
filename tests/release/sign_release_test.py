# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

"""Exercise release signing with disposable encrypted P-256 credentials."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


SIGNER = Path(__file__).resolve().parents[2] / "tools/release/sign_release.sh"
SIGN_COMMAND = (
    'openssl dgst -sha256 -sign "$ACTRUST_RELEASE_PRIVATE_KEY_FILE" '
    '-passin "file:$ACTRUST_RELEASE_PASSPHRASE_FILE" '
    '-out "$ACTRUST_SIGN_OUTPUT" "$ACTRUST_SIGN_INPUT"'
)
VERIFY_COMMAND = (
    'openssl dgst -sha256 -verify "$ACTRUST_RELEASE_PUBLIC_KEY_FILE" '
    '-signature "$ACTRUST_VERIFY_SIGNATURE" "$ACTRUST_VERIFY_INPUT"'
)


class ReleaseSigning(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.keys = tempfile.TemporaryDirectory(prefix="actrust-test-keys-")
        cls.addClassCleanup(cls.keys.cleanup)
        root = Path(cls.keys.name)
        # Shell metacharacters must stay literal when supplied by a secret.
        cls.passphrase = 'test-only $(false) `false` ; $quoted " password'
        password = root / "password.txt"
        password.write_text(cls.passphrase + "\n", encoding="utf-8")
        private = root / "private.pem"
        public = root / "public.pem"
        subprocess.run([
            "openssl", "genpkey", "-algorithm", "EC", "-pkeyopt",
            "ec_paramgen_curve:P-256", "-aes-256-cbc", "-pass",
            f"file:{password}", "-out", str(private),
        ], check=True, capture_output=True)
        subprocess.run([
            "openssl", "pkey", "-in", str(private), "-passin",
            f"file:{password}", "-pubout", "-out", str(public),
        ], check=True, capture_output=True)
        cls.private_key = private.read_text(encoding="utf-8")
        cls.public_key = public.read_text(encoding="utf-8")

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="actrust signing test ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.scratch = self.root / "runner temp"
        self.scratch.mkdir()
        self.bundle = self.root / "release bundle.tar.gz"
        self.bundle.write_bytes(b"release content\n")
        self.signature = self.root / "release bundle.tar.gz.sig"
        self.public = self.root / "public key.pem"
        self.public.write_text(self.public_key, encoding="utf-8")
        self.env = os.environ.copy()
        self.env.update({
            "ACTRUST_SIGNING_PRIVATE_KEY": self.private_key,
            "ACTRUST_SIGNING_PASSPHRASE": self.passphrase,
            "ACTRUST_SIGN_COMMAND": SIGN_COMMAND,
            "ACTRUST_VERIFY_COMMAND": VERIFY_COMMAND,
            "RUNNER_TEMP": str(self.scratch),
        })

    def run_signer(self, trace=False):
        command = ["bash"] + (["-x"] if trace else []) + [
            str(SIGNER), str(self.bundle), str(self.signature), str(self.public),
        ]
        result = subprocess.run(
            command, env=self.env, capture_output=True, text=True, timeout=15,
        )
        self.assertEqual(list(self.scratch.iterdir()), [], "key files remain")
        output = result.stdout + result.stderr
        self.assertNotIn(self.passphrase, output)
        self.assertNotIn(self.private_key, output)
        return result

    def test_success_restricts_permissions_and_verifies_signature(self):
        self.env["ACTRUST_SIGN_COMMAND"] = (
            'test "$(stat -c %a "$ACTRUST_RELEASE_PRIVATE_KEY_FILE")" = 600; '
            'test "$(stat -c %a "$ACTRUST_RELEASE_PASSPHRASE_FILE")" = 600; '
            'test "$(stat -c %a "$(dirname '
            '"$ACTRUST_RELEASE_PRIVATE_KEY_FILE")")" = 700; '
            'test -z "${ACTRUST_SIGNING_PRIVATE_KEY+x}"; '
            'test -z "${ACTRUST_SIGNING_PASSPHRASE+x}"; ' + SIGN_COMMAND
        )
        result = self.run_signer(trace=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertGreater(self.signature.stat().st_size, 0)
        subprocess.run([
            "openssl", "dgst", "-sha256", "-verify", str(self.public),
            "-signature", str(self.signature), str(self.bundle),
        ], check=True, capture_output=True)

    def test_bad_passphrase_fails_and_cleans_up(self):
        self.env["ACTRUST_SIGNING_PASSPHRASE"] = "incorrect passphrase"
        self.assertNotEqual(self.run_signer().returncode, 0)

    def test_tampered_bundle_fails_verification_and_cleans_up(self):
        self.env["ACTRUST_SIGN_COMMAND"] = (
            SIGN_COMMAND + '; printf tampered >>"$ACTRUST_SIGN_INPUT"'
        )
        result = self.run_signer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Verification failure", result.stdout + result.stderr)

    def test_missing_secret_fails_without_leaving_files(self):
        del self.env["ACTRUST_SIGNING_PRIVATE_KEY"]
        result = self.run_signer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("private key and passphrase are required", result.stderr)
        self.assertFalse(self.signature.exists())

    def test_terminated_signing_command_cleans_up(self):
        self.env["ACTRUST_SIGN_COMMAND"] = 'kill -TERM "$PPID"'
        self.assertNotEqual(self.run_signer().returncode, 0)


if __name__ == "__main__":
    unittest.main()
