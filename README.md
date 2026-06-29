# AntChainTrustSDK

[![License: Apache 2.0](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE) [![Standard: C99](https://img.shields.io/badge/C-99-A8B9CC?logo=c&logoColor=white)](https://www.iso.org/standard/29237.html) [![Target](https://img.shields.io/badge/target-Linux%20%7C%20Android%20%7C%20SIMCom-1f6feb)](README.md#quick-start-linux-x86_64)

A modular IoT framework in **C99** providing platform abstraction, component
libraries, and third-party integrations for embedded systems.

AntChainTrustSDK is designed as a lightweight, multi-cloud compatible trusted on-chain
SDK for resource-constrained embedded devices. It acts as a trusted anchor
between physical devices and blockchain networks, helping IoT data remain
tamper-evident at the source, attributable to its originating device, and ready
for secure submission to cloud and blockchain-backed workflows.

AntChainTrustSDK supports generic Linux (`linux_x86` and `linux_arm`), Android
NDK builds, and SIMCom A7606E-H (ARM Cortex-A7, OpenWrt + musl libc), and is
designed to be portable to other POSIX-like embedded targets with minimal
effort.

---

## Highlights

- **Single-binary cloud client** -- registration, MQTT, TLS, and downlink dispatch
  exposed behind a tiny async API (`actrust_init`, `actrust_connect`, `actrust_register`,
  `actrust_data_publish`).
- **Clean platform abstraction** -- `network`, `storage`, `security`, `system`,
  `device` interfaces let you port to a new SoC by implementing five headers.
- **Composable components** -- `cloud`, `mqtt`, `tls`, `crypto`, `kv`, `log`,
  `ntp`, `queue`, `json` -- each one a standalone CMake target with unit and
  smoke tests.
- **Kconfig-driven configuration** -- `make menuconfig`-style build flags,
  generated into both `actrust_config.h` and `actrust_config.cmake`.
- **Curated third-party stack** -- coreMQTT, coreMQTT-Agent, coreJSON, coreSNTP,
  backoffAlgorithm, mbedTLS -- all vendored as submodules under `3rdparts/`.

---

## Repository Layout

```
AntChainTrustSDK/
├── build.sh                    Build entry point
├── CMakeLists.txt              Top-level CMake project
├── Kconfig                     Top-level Kconfig
├── LICENSE                     Apache-2.0
├── NOTICE                      Third-party attributions
├── include/                    Public umbrella headers (actrust.h, actrust_errno.h)
├── source/
│   ├── core/                   Core lifecycle / job dispatcher
│   ├── components/             cloud, mqtt, tls, crypto, kv, log, ntp, queue, json
│   └── adapter/
│       ├── include/adapter/    Abstract platform interfaces (headers only)
│       └── platform/
│           ├── android/        Android NDK implementation
│           ├── linux/          Generic Linux (glibc) implementation
│           └── simcom/a7606e/  SIMCom A7606E-H (musl + SIMCom SDK)
├── 3rdparts/                   Vendored submodules (mbedTLS, coreMQTT, ...)
├── config/                     Per-platform Kconfig defconfigs
├── cmake/                      Toolchain files (cross-compilation)
├── examples/                   Worked usage examples
├── tools/                      format.sh, genconfig.sh
└── tests/                      Unit and smoke test scaffolding
```

---

## Quick Start (Linux x86_64)

### Prerequisites

- CMake >= 3.16
- A C99 compiler (GCC 9+ or Clang 10+)
- Python 3.8+ (for Python-based developer tools such as `gersemi`)
- `clang-format`, `shfmt`, `gersemi` (only required if you intend to run
  `tools/format.sh`)

### Build & test

```bash
git clone --recurse-submodules https://github.com/antgroup/AntChainTrustSDK.git
cd AntChainTrustSDK
./build.sh linux_x86 --clean-build --test
```

This will:
1. Copy `config/linux_defconfig` to `.config`
2. Generate `build/config/actrust_config.{h,cmake}` from `.config`
3. Configure & build under `build/`
4. Run all `ctest --label-regex actrust` tests

Run a single test:

```bash
ctest --test-dir build --output-on-failure -R cloud_smoke_test
```

### Interactive configuration

Open the menu-based configuration UI to customise build options:

```bash
sudo apt install kconfig-frontends   # one-time, provides kconfig-mconf
kconfig-mconf Kconfig                # opens the ncurses menu
```

### Re-build without re-running Kconfig

```bash
./build.sh --skip-config
```

---

## Cross-Compilation (SIMCom A7606E-H)

The [SIMCom A7606E-H](https://www.simcom.com/) is a 4G CAT-4 module running
OpenWrt + musl libc on an ARM Cortex-A7.

```bash
export ACTRUST_TOOLCHAIN_PATH=/path/to/arm-openwrt-linux
./build.sh simcom_a7606e --clean-build
```

The default toolchain file is `cmake/toolchain-simcom-a7606e.cmake`. Set
`ACTRUST_TOOLCHAIN_PATH` to point at the unpacked OpenWrt toolchain root (the
directory that contains `bin/arm-openwrt-linux-muslgnueabi-gcc`).

---

## Cross-Compilation (Android)

Android builds use the Android NDK CMake toolchain and the adapter under
`source/adapter/platform/android/`.

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk
./build.sh android --clean-build
```

The build defaults to `ANDROID_ABI=arm64-v8a` and
`ANDROID_PLATFORM=android-23`. Override those environment variables when a
different ABI or API level is required. The Android adapter stores development
data under `/data/local/tmp/actrust/storage` and security slots under
`/data/local/tmp/actrust/security` by default.

---

## Adding a New Platform

1. Implement the five adapter interfaces in `source/adapter/platform/<your-platform>/`:
   - `device.c` -- hardware ID, model, firmware version
   - `network.c` -- TCP/UDP sockets, DNS
   - `security.c` -- secure storage, key management, crypto primitives
   - `storage.c` -- block-level persistent storage
   - `system.c` -- mutex, semaphore, task, time, log output
2. Add a `CMakeLists.txt` that builds the platform implementation as `adapter`.
3. Add `config/<your-platform>_defconfig` describing the build configuration.
4. (Optional) Add a CMake toolchain file under `cmake/` for cross-compilation.

The Linux adapter at `source/adapter/platform/linux/` is the reference
implementation; copy and adapt it.

---

## Security Adapter Notice

The Linux and Android `security.c` adapters are file-backed
reference/development implementations. They are not production secure-store or
key-isolation boundaries, and they do not provide hardware-backed
confidentiality, anti-tamper protection, rollback protection, or non-exportable
private-key storage.

Production deployments that need to protect private keys must replace or extend
these adapters with the target platform's actual Android Keystore, TEE, secure
element, TPM, or equivalent secure storage/key-management service. Changing only
`CONFIG_ACTRUST_SEC_STORE_BASE_DIR` changes the file location; it does not make
software private-key slots production-safe.

---

## API Sketch

Register an async completion callback with `actrust_set_callback()`. The
callback receives the operation result and the current Core state; application
code should use that event to decide the next SDK call from its own main loop or
task context.

A complete, runnable version of this flow lives in
[`examples/hello_actrust.c`](examples/hello_actrust.c). Build it with:

```bash
./build.sh linux_x86 --clean-build          # build the libraries first
cmake -S . -B build -DACTRUST_BUILD_EXAMPLES=ON
cmake --build build --target actrust_example_hello
```

Examples are off by default (`ACTRUST_BUILD_EXAMPLES=OFF`) so they never affect
the normal library/test build.

---

## API Documentation

API docs are generated with Doxygen from the public headers, component
interfaces, selected internal headers, and examples:

```bash
doxygen Doxyfile        # output written to docs/html/ (git-ignored)
```

---

## Error Model

All public APIs return `actrust_err_t` (32-bit packed code):

```
[31:16] module_id    e.g. ACTRUST_ERR_MODULE_COMPONENTS_MQTT (0x2101)
[15: 0] reason_code  e.g. ACTRUST_ERR_MQTT_CONNECT_FAILED    (0x0200)
```

Inspect with `ACTRUST_ERR_MODULE(e)` and `ACTRUST_ERR_CODE(e)`. The full table is in
[`include/actrust_errno.h`](include/actrust_errno.h).

---

## Contributing

Contributions are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) for
coding conventions, the commit message style (`[verb] description`), the format
toolchain (`tools/format.sh`), and how to add a new component.

For security issues, **do not open a public issue** -- please read
[SECURITY.md](SECURITY.md) for the responsible disclosure process.

---

## License

Copyright 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for the
full text, and [NOTICE](NOTICE) for third-party attributions.
