# Test PKI Material

Four smoke tests require X.509 material (certificates **and** private keys)
that is **not** checked into this repository:

| Test | Files expected under |
| --- | --- |
| `core_smoke_test` | `tests/pki/` |
| `cloud_smoke_test` | `tests/pki/` |
| `mqtt_smoke_test` | `tests/pki/` |
| `tls_smoke_test` | `tests/pki/` |

If the expected `client.*` files or required AWS config values are absent at
CMake-configure time, the test executable still builds and is registered with
CTest as **skipped** -- `./build.sh --test` will report it as skipped instead
of failing.

## Shared runtime material

`tests/pki/` holds material shared by the PKI-gated smoke tests. The public
root (`AmazonRootCA.pem`) is safe to commit; local `client.*` files are
user-supplied and ignored by git. CMake copies the available PKI files once
into the shared runtime directory `build/tests/pki/`.

---

## What each test needs (private, user-supplied)

All four smoke tests share the same two files:

- `client.crt` -- AWS IoT **claim** certificate (PEM)
- `client.key` -- corresponding EC private key (PEM)

All four tests require `CONFIG_ACTRUST_CLOUD_AWS_ENDPOINT`. `core_smoke_test`
and `cloud_smoke_test` also require `CONFIG_ACTRUST_CLOUD_AWS_TEMPLATE_NAME` and
`CONFIG_ACTRUST_CLOUD_AWS_PRODUCT_KEY`.

The claim cert is issued by an AWS IoT *provisioning template* that is
attached to your account. See
<https://docs.aws.amazon.com/iot/latest/developerguide/provision-wo-cert.html>.

`core_smoke_test` and `cloud_smoke_test` drive the fleet-provisioning by
claim flow. `mqtt_smoke_test` publishes a CSR to
`$aws/certificates/create-from-csr/json`. `tls_smoke_test` exercises mTLS
against the AWS IoT endpoint (PEM-to-DER conversion is done at runtime).

---

## Generating your own material

For the four tests above you must use AWS IoT -- they verify behaviour
against a real AWS endpoint, not a local mock. The typical flow:

1. In the AWS IoT console, create a *thing*, attach a *policy* that allows
   `iot:Connect`, `iot:Publish`, `iot:Subscribe`, `iot:Receive`, and the
   `$aws/certificates/create-from-csr/json` and provisioning-template
   topics.
2. Download the device cert + private key.
3. Drop them in `tests/pki/` with the expected filenames.
4. Set the matching `CONFIG_ACTRUST_CLOUD_AWS_*` values in `.config` or the
   selected defconfig.
5. Re-run `cmake` so the configure-time guards re-evaluate, then build & test
   as usual.

---

## Why these files are not committed

Private keys and device certificates are sensitive credentials. Keeping
them out of git protects users from accidentally signing into someone
else's AWS account, and protects the project from credential leakage.

`.gitignore` blocks local PKI material such as `tests/**/pki/client.*`,
`*.crt*`, `*.der*`, `*.key*`, and `*.csr*`; do not bypass these rules.
