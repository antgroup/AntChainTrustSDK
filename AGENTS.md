# Repository Guidelines

## Project Overview

AntChainTrustSDK is a C99 IoT framework built with CMake and Kconfig. It is
designed as a lightweight, multi-cloud compatible trusted on-chain SDK for
resource-constrained embedded devices, acting as a trusted anchor between
physical devices and blockchain networks for tamper-evident, attributable, and
secure IoT data submission.

The SDK provides platform abstraction, component libraries, and third-party
integrations for embedded systems. Supported targets include generic Linux
(`linux_x86` and `linux_arm`), Android NDK builds, and the SIMCom A7606E-H
platform, which uses an ARM Cortex-A7, OpenWrt Linux, and musl libc.

## Project Structure & Module Organization

Public umbrella headers live in `include/`. Implementation code is under
`source/`: `core/` contains lifecycle and dispatcher code, `components/`
contains libraries such as `cloud`, `mqtt`, `tls`, `crypto`, `kv`, `log`,
`ntp`, `queue`, and `json`, and `adapter/` contains common platform
interfaces plus Linux, Android, and
SIMCom implementations. Platform defaults are in `config/`, toolchain files in
`cmake/`, examples in `examples/`, helper scripts in `tools/`, vendored
submodules in `3rdparts/`, and tests in `tests/`.

Do not restyle or refactor `3rdparts/` unless intentionally updating a
submodule.

## Build, Test, and Development Commands

Run all commands from the repository root:

```bash
./build.sh [PLATFORM] [OPTIONS]              # See --help for full options
./build.sh linux_x86 --clean-build --test    # Linux clean build + tests
ANDROID_NDK_HOME=/path/to/android-ndk ./build.sh android
./build.sh simcom_a7606e --clean-build       # A7606E cross-compile
./build.sh --skip-config                     # Rebuild without Kconfig regen
./build.sh --clean                           # Remove build/ and exit
tools/format.sh                              # Format C, shell, and CMake
tools/genconfig.sh                           # Regenerate generated config
doxygen Doxyfile                             # Generate API docs
```

The optional `PLATFORM` argument selects a build target and copies its mapped
defconfig to `.config` before building. Omitting it uses the existing `.config`.
Available platforms include `linux_x86`, `linux_arm`, `android`, and
`simcom_a7606e`.

Run one test after building:

```bash
ctest --test-dir build --output-on-failure -R <test_name>
```

Common test selections:

```bash
ctest --test-dir build --output-on-failure -R queue_unit_test
ctest --test-dir build --output-on-failure --label-regex unit
ctest --test-dir build --output-on-failure --label-regex actrust
```

## Configuration System

Configuration is Kconfig-based. The source of truth is `.config`. Running
`tools/genconfig.sh`, also invoked by `./build.sh`, generates:

- `build/config/actrust_config.h`: C `#define` directives.
- `build/config/actrust_config.cmake`: CMake `set()` variables.

Do not edit generated `build/config/actrust_config.{h,cmake}` files directly.
Change configuration through `.config`, `config/*_defconfig`, or Kconfig
tooling, then regenerate with `tools/genconfig.sh` or the build script.

Platform defconfigs live in `config/`, for example `linux_defconfig`,
`android_defconfig`, and `simcom_a7606e_defconfig`. To add a new platform,
create a defconfig there and add the platform mapping in `build.sh`.
`CMakeLists.txt` files use `if(CONFIG_...)` guards for conditional compilation.

## Architecture

The top-level umbrella header `include/actrust.h` exposes the async client API:
`actrust_set_callback`, `actrust_init`, `actrust_deinit`, `actrust_connect`,
`actrust_disconnect`, `actrust_register`, and `actrust_data_publish`.
Bootstrap configuration is `actrust_config_t`. A worked end-to-end example
lives at `examples/hello_actrust.c`.

`source/core/` implements the public API on top of a job dispatcher and service
task. Public functions in `core_api.c` enqueue `core_job_*` jobs that the
service task drains. Internal helpers use the unprefixed naming convention,
such as `core_set_state` and `core_post_job`, and live behind
`core/core_internal.h`. Do not call core internals from components.

`source/adapter/include/` defines abstract platform interfaces:

- `device.h`: hardware ID, model, revision, firmware version.
- `network.h`: TCP/UDP sockets and DNS resolution.
- `security.h`: secure storage, key management, and crypto operations.
- `storage.h`: block-level persistent storage.
- `system.h`: mutex, semaphore, task, time, and log output.

Platform implementations live under `source/adapter/platform/<platform>/`:

- `linux/`: generic Linux using POSIX APIs, file-backed storage at
  `.actrust/storage`, and secure storage at `.actrust/security`.
- `android/`: Android NDK build using POSIX APIs where available plus Android
  log output, file-backed storage at `/data/local/tmp/actrust/storage`, and
  security slots at `/data/local/tmp/actrust/security`.
- `simcom/a7606e/`: SIMCom A7606E-H using POSIX APIs plus the SIMCom SDK
  (`libsdk.a`) for IMEI, with file-backed storage at `/data/actrust/storage`
  and secure storage at `/data/actrust/security` (backed by the Sunsea TEE).

The SIMCom A7606E-H toolchain uses GCC 13.3.0, musl 1.2.5, and ARM Cortex-A7
hard-float. `cmake/toolchain-simcom-a7606e.cmake` is auto-selected by
`./build.sh` when the SIMCom platform is active. Toolchain root resolution uses
the first available value:

1. CMake cache variable `-DACTRUST_TOOLCHAIN_PATH=<path>`.
2. Environment variable `ACTRUST_TOOLCHAIN_PATH=<path>`.
3. In-tree fallback `<repo>/toolchain/arm-openwrt-linux`.

The A7606E adapter links against `libsdk.a` and OpenWrt libraries from the
toolchain sysroot at `<toolchain>/sysroots-target-arm/usr/lib/`.

Android builds use the Android NDK CMake toolchain. Set `ANDROID_NDK_HOME` or
`ANDROID_NDK_ROOT` before running `./build.sh android`; the default ABI is
`arm64-v8a` and the default API level is `android-23`.

Each component in `source/components/<name>/` follows this layout:

- `include/<name>/`: public headers.
- `source/`: implementation.
- `CMakeLists.txt`: static library target.
- `Kconfig`: component config options.

Component tests live under `tests/components/<name>/` and usually include both
`<name>_unit_test.c` and `<name>_smoke_test.c`, registered by that directory's
`CMakeLists.txt`.

Component dependency order, high to low:

```text
cloud -> mqtt, crypto, json, kv, tls, queue, log, adapter
mqtt  -> tls, queue, log, crypto, coreMQTT-Agent, backoffAlgorithm, adapter
tls   -> crypto, adapter (network), mbedTLS
crypto -> adapter (security), mbedTLS (SW) or TEE (HW)
kv    -> adapter (storage)
ntp   -> adapter (network), coreSNTP
json  -> coreJSON
queue -> adapter (system)
log   -> adapter (system)
```

Third-party libraries live under `3rdparts/` as git submodules: coreMQTT,
coreMQTT-Agent, coreJSON, coreSNTP, mbedTLS, backoffAlgorithm, and Unity.

## Coding Style & Naming Conventions

Use strict C99 with `-Wall -Wextra -Werror -Wpedantic`. Follow 4-space
indentation, no tabs, a soft 80-character line limit, Egyptian braces for
control flow, next-line braces for function definitions, and braced
single-line bodies. Use `type *ptr` pointer style, `(type) value` cast style,
and fixed-width integer types where size matters.

Public APIs use `actrust_<module>_<action>` names and return `actrust_err_t`.
Internal helpers usually omit the `actrust_` prefix, for example
`core_set_state`. Types follow `actrust_<module>_<name>_t`, opaque handles use
`typedef struct actrust_xxx_ctx *actrust_xxx_t`, macros and constants use
uppercase snake case with an `ACTRUST_` prefix, and header guards use
`ACTRUST_<MODULE>_H`.

The unified `actrust_err_t` error type is `uint32_t` with `[31:16]` as the
module ID and `[15:0]` as the reason code. It is defined in
`include/actrust_errno.h`. Each module defines a local helper similar to:

```c
#define MOD_ERR(reason) ACTRUST_ERR(ACTRUST_ERR_MODULE_..., (reason))
```

Validate arguments early and return explicit error codes. Use cleanup labels
such as `fail:`, `out:`, or `cleanup:` for resource cleanup. Cast to `(void)`
when intentionally ignoring a return value.

Use this include order, with a blank line between groups and a `/* Group */`
comment before each present group:

1. `/* C standard */`: `<stdio.h>`, `<stdlib.h>`, and similar headers.
2. `/* Third-party */`: mbedTLS and vendored headers.
3. `/* Common */`: `"common/common.h"`.
4. `/* Project */`: `"actrust.h"`, `"actrust_config.h"`,
   `"actrust_errno.h"`.
5. `/* <ModuleName> */`: the module's own headers.
6. `/* Component */`: cross-component dependencies.
7. `/* Adapter */`: adapter headers.

Skip absent groups and sort alphabetically within each group.

Public headers use Doxygen `@file` and `@brief` comments plus `extern "C"`
guards. New source, header, build, configuration, and script files need a
two-line REUSE-compliant SPDX header. `SPDX-FileCopyrightText` comes first,
`SPDX-License-Identifier` second, and both lines use the same comment style.

For C:

```c
// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0
```

For shell, CMake, Kconfig, `.cfg`, and Python files:

```text
# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0
```

The full Apache-2.0 text lives at `LICENSE`. Do not revert to the older
`// SPDX` plus `/* Copyright */` block form.

## Testing Guidelines

Tests use Unity from `3rdparts/Unity` and mirror the source layout. Name fast
isolated tests `<name>_unit_test.c` and integration-flavoured tests
`<name>_smoke_test.c`. Register tests with CTest labels such as `actrust`,
`unit`, `smoke`, `adapter`, `component`, or `core`. PKI-gated smoke tests must
skip cleanly when private credential files are absent.

## Commit & Pull Request Guidelines

Commit subjects follow `[verb] description`, where `verb` is `add`, `update`,
`fix`, or `format`; keep subjects under 72 characters, for example
`[fix] core: validate null payload before publish`. Pull requests should include
a clear description, linked issue when applicable, target platform/toolchain
details for build issues, and confirmation that `tools/format.sh` and
`./build.sh linux_x86 --clean-build --test` pass.

## Security & Configuration Tips

Do not commit secrets, device certificates, private keys, CSRs, or generated
`build/config/actrust_config.{h,cmake}` files. Do not commit private credential
files used by PKI-gated smoke tests.
