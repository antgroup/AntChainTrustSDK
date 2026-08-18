# AntChainTrustSDK Tests

Tests for every AntChainTrustSDK layer — adapter, core, and components. The directory
mirrors `source/` so a test for `<foo>` lives next to where one would expect
to find it.

Each component (and each adapter interface) has up to two executables:

- `<name>_smoke_test` — integration-flavoured: real network, real PKI, real
  long-running tasks. Labelled `smoke`. Some require external state and are
  skipped if PKI material is absent (see below).
- `<name>_unit_test` — fast, isolated checks of argument validation, error
  paths, and edge cases. Labelled `unit`. Always runnable on the host.

Both kinds use the [Unity](https://github.com/ThrowTheSwitch/Unity) test
framework (vendored at `3rdparts/Unity`).

## Layout

```
tests/
├── CMakeLists.txt              Root entry.
├── README.md                   This file.
├── common/                     Shared test helpers (header-only).
│   └── actrust_test.h          actrust_test_load_file() definition.
├── pki/                        Shared PKI material for PKI-gated tests.
│   ├── AmazonRootCA.pem
│   └── PKI.md                  How to supply private credentials for the
│                               four PKI-gated tests.
├── adapter/                    Per-interface adapter tests.
│   ├── CMakeLists.txt
│   ├── {system,network,storage,security,device}_smoke_test.c
│   └── {system,network,storage,security,device}_unit_test.c
├── core/                       Public-API lifecycle.
│   ├── CMakeLists.txt
│   ├── core_smoke_test.c
│   └── core_unit_test.c
└── components/
    ├── CMakeLists.txt          Kconfig-gated fan-out.
    └── <name>/
        ├── CMakeLists.txt
        ├── <name>_smoke_test.c
        └── <name>_unit_test.c
```

## Running

All commands run from the repo root. Build and run every test:

```sh
./build.sh linux_x86 --clean-build --test
```

Build, then run only one layer:

```sh
./build.sh --test-adapter        # adapter tests only
./build.sh --test-component      # component tests only
```

Run a single test by name (after a build):

```sh
ctest --test-dir build --output-on-failure -R queue_unit_test
```

## CTest label taxonomy

CTest labels have three independent dimensions. Keep all applicable labels on
new tests so CI can select tests by code layer, test form, or environment
requirements without maintaining a test-name list.

### Code layer

These labels identify which part of the SDK is under test:

- `actrust` — every AntChainTrustSDK test.
- `adapter` — platform abstraction tests.
- `component` — protocol and utility component tests.
- `core` — public Core API lifecycle tests.
- `common` — shared test helper tests.

### Test form

These labels identify how the test is intended to run:

- `unit` — fast, deterministic, host-side checks; the offline CI gate.
- `smoke` — higher-level checks that may use local or external resources.

### Environment dependency

These labels identify external prerequisites, independently of code layer and
test form:

- `network` — accesses DNS or network endpoints. Current examples are
  `network_adapter_smoke_test` and `ntp_smoke_test`.
- `integration` — requires an external service or a real end-to-end protocol
  path. Current examples are the AWS/PKI smoke tests.
- `aws` — requires AWS IoT configuration or service access.
- `pki` — requires user-supplied test certificates/private keys under
  `tests/pki/`.

`network` is not a synonym for `integration`: network and NTP tests need network
access but do not require AWS or PKI credentials. AWS tests carry
`integration;aws;pki` together. Do not add `external` as a catch-all label; use
the specific environment label instead.

Current selection examples:

```sh
# C04 offline gate
ctest --test-dir build --output-on-failure --label-regex unit

# Network-dependent job
ctest --test-dir build --output-on-failure --label-regex network

# AWS/PKI integration job
ctest --test-dir build --output-on-failure --label-regex aws

# Smoke tests excluding environment-dependent tests
ctest --test-dir build --output-on-failure \
      --label-regex actrust --label-exclude 'network|integration'
```

The `unit` label is the required offline baseline. The `network` and `aws`
selections are optional jobs for environments that provide their prerequisites.
AWS tests without credentials remain selected but use the existing configure-time
CTest skip behavior.

## Test result states

`PASS` means the test completed successfully; `FAIL` means an assertion or
unexpected error occurred; `SKIP` means a documented prerequisite was absent or
an external resource was unavailable; `NOT RUN` means the requested test was
not selected or was not built. A Unity `TEST_IGNORE_MESSAGE` is reported within
its executable, while configure-time CTest skips use `SKIP_REGULAR_EXPRESSION`.


## PKI-gated tests

Four smoke tests (`core_smoke_test`, `cloud_smoke_test`, `mqtt_smoke_test`,
`tls_smoke_test`) need real AWS IoT credentials and AWS config. They are
registered as **skipped** when expected `client.*` files or required
`CONFIG_ACTRUST_CLOUD_AWS_*` values are absent. They still build; they just exit
immediately with a `Skipped` message. See [`pki/PKI.md`](pki/PKI.md) for what
each test wants and how to obtain it.

Sensitive material (`client.*`, `*.crt*`, `*.der*`, `*.key*`, `*.csr*`) is blocked by
`.gitignore` — do not bypass these rules.

## Runtime State Isolation

CTest runs each AntChainTrustSDK test from its own directory under `build/tests/runtime/`.
For Linux test builds, configure storage as relative paths such as
`.actrust/storage` and `.actrust/security`; those paths resolve under the per-test
runtime directory, not under the developer's home directory. A clean build
removes the runtime tree with the rest of `build/`; when rerunning CTest without
`--clean-build`, remove `build/tests/runtime/` manually if a fresh runtime state
is required.

## Shared scaffolding

Tests link against:

- `unity` — the Unity framework, providing `UNITY_BEGIN()`,
  `RUN_TEST(fn)`, `UNITY_END()`, and the `TEST_ASSERT_*` family.
  Built in `3rdparts/CMakeLists.txt` (gated on `BUILD_TESTING`).
- `tests/common/actrust_test.h` — a header-only helper exposing
  `actrust_test_load_file()` for reading PKI material from disk. The four
  PKI-gated smoke tests include it via `target_include_directories`
  pointing at `tests/common/`.

## Adding a new test

Use `tests/components/queue/` as the minimal template:

1. Create `tests/<layer>/<name>/<name>_unit_test.c` (or `_smoke_test.c`).
   Open with the REUSE/SPDX two-liner and the AGENTS.md include groups
   (`/* C standard */`, `/* Third-party */`, `/* Common */`,
   `/* <Module> */`, `/* Component */`, `/* Adapter */` — skip absent groups).
2. Define `setUp(void)` and `tearDown(void)` (may be empty), then per-test
   `void test_<name>(void)` functions using `TEST_ASSERT_*`. Drive them
   from `main()` with `UNITY_BEGIN(); RUN_TEST(test_x); return UNITY_END();`.
3. Create or extend `tests/<layer>/<name>/CMakeLists.txt`:

   ```cmake
   add_executable(<name>_unit_test
                  "${CMAKE_CURRENT_LIST_DIR}/<name>_unit_test.c")
   target_link_libraries(<name>_unit_test PRIVATE unity <name>)
   actrust_add_test(NAME <name>_unit_test COMMAND <name>_unit_test)
   set_tests_properties(<name>_unit_test PROPERTIES LABELS "actrust;component;unit")
   ```

4. For a component, wire it into `tests/components/CMakeLists.txt` behind
   its `CONFIG_ACTRUST_COMPONENTS_<NAME>` Kconfig guard.
5. If the test needs PKI material, copy the pattern from
   `tests/components/cloud/CMakeLists.txt` (shared runtime PKI via
   `actrust_use_shared_pki`, private material gated by `if(EXISTS ...)`),
   add `target_include_directories(<name> PRIVATE "${CMAKE_SOURCE_DIR}/tests/common")`
   to access `actrust_test_load_file()`, and update
   [`pki/PKI.md`](pki/PKI.md).
