# Contributing to AntChainTrustSDK

Thank you for your interest in contributing. This document describes the
conventions and the workflow we expect from pull requests.

By submitting a contribution you agree that it is licensed under the
**Apache License 2.0** (see [LICENSE](LICENSE)) and that you have the right to
license it under those terms.

---

## Reporting Issues

- **Bug reports** -- open a GitHub issue with: target platform, toolchain version,
  exact build command, `.config` (or defconfig used), and the full failing log.
- **Security issues** -- **do not file a public issue**. Follow [SECURITY.md](SECURITY.md).
- **Feature requests** -- open an issue describing the use case before writing code,
  especially for changes that affect public headers or the adapter interface.

---

## Development Workflow

1. Fork the repository and create a topic branch off `main`.
2. Make focused commits -- one logical change per commit.
3. Run `tools/format.sh` before each commit.
4. Run `./build.sh linux_x86 --clean-build --test` and make sure all tests pass.
5. Open a pull request against `main`.

---

## Coding Conventions

The project is strict C99, compiled with `-Wall -Wextra -Werror -Wpedantic`.
Pull requests that warn or fail under these flags will not be merged.

### Naming

| Item | Pattern | Example |
|------|---------|---------|
| Public functions | `actrust_<module>_<action>` | `actrust_mqtt_connect` |
| Internal (static / non-public) functions | `<module>_<action>` | `core_set_state` |
| Types | `actrust_<module>_<name>_t` | `actrust_mqtt_config_t` |
| Opaque handles | `typedef struct actrust_xxx_ctx *actrust_xxx_t` | `actrust_cloud_t` |
| Header guards | `ACTRUST_<MODULE>_H` | `ACTRUST_CLOUD_H` |
| Macros / constants | Uppercase snake case with module prefix | `ACTRUST_MQTT_MAX_TOPIC_LEN` |

### Style

- 4-space indent, no tabs.
- 80-character line width (soft limit).
- **Egyptian braces** for control flow; **next-line braces** for function definitions.
- Always brace single-line bodies.
- Pointer style: `type *ptr`. Cast style: `(type) value`.
- Use explicit fixed-width types (`uint32_t`, `int16_t`, ...). Prefer `size_t` for
  lengths, `ssize_t` for return values that may be negative.
- Cast to `(void)` when intentionally ignoring a return value.
- Use cleanup labels (`fail:`, `out:`, `cleanup:`) for resource cleanup paths.
- Public headers must declare `@file` and `@brief` in a top-level Doxygen block,
  and wrap declarations with `extern "C"` guards.

### Include Order

Each include group is separated by a blank line and headed by a `/* Group */`
comment label. Skip absent groups. Sort alphabetically within a group.

1. `/* C standard */`     -- `<stdio.h>`, `<stdlib.h>`, ...
2. `/* Third-party */`    -- `<mbedtls/...>`, `"core_json.h"`, ...
3. `/* Common */`         -- `"common/common.h"`
4. `/* Project */`        -- `"actrust.h"`, `"actrust_config.h"`, `"actrust_errno.h"`
5. `/* <ModuleName> */`   -- module's own headers (label is the module name)
6. `/* Component */`      -- cross-component dependencies
7. `/* Adapter */`        -- `"adapter/system.h"`, `"adapter/network.h"`, ...

### File Headers

Every source/header file must start with a two-line
[REUSE](https://reuse.software)-compliant SPDX header.
`FileCopyrightText` comes first, `License-Identifier` second; both lines use
the same comment style.

```c
// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0
```

For hash-comment files (`CMakeLists.txt`, `Kconfig`, `*.sh`, `*.cmake`, ...):

```
# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0
```

In shell scripts, the SPDX block goes immediately after the shebang line.

Public C headers add a Doxygen file block after the SPDX header:

```c
// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file mqtt.h
 * @brief Asynchronous MQTT client built on coreMQTT-Agent.
 */
```

### Error Handling

- All public APIs return `actrust_err_t`. Never return raw `int` or POSIX `errno`
  values across module boundaries.
- Each module defines a local helper `MOD_ERR(reason)` that expands to
  `ACTRUST_ERR(ACTRUST_ERR_MODULE_<NAME>, reason)`.
- Validate arguments at the top of every public function and return
  `ACTRUST_ERR_INVALID_ARG` for NULL or out-of-range inputs.
- Do not silently swallow errors. Always propagate or log with a non-debug level.

---

## Tooling

- `./build.sh [platform] [options]` -- build entry point.
- `tools/format.sh` -- runs clang-format, shfmt, and gersemi. Must pass cleanly
  before every commit.
- `tools/genconfig.sh` -- regenerate `build/config/actrust_config.h` and
  `build/config/actrust_config.cmake` from `.config`.

Do **not** edit the generated `actrust_config.h` or `actrust_config.cmake` directly.

Do **not** restyle or refactor code under `3rdparts/` -- those are vendored
submodules and must remain upstream-clean.

---

## Adding a New Component

1. Create `source/components/<name>/` and `tests/components/<name>/` with the
   standard layout:

   ```
   source/components/<name>/
   ├── CMakeLists.txt
   ├── Kconfig
   ├── include/<name>/<name>.h
   └── source/<name>.c

   tests/components/<name>/
   ├── CMakeLists.txt
   ├── <name>_smoke_test.c
   └── <name>_unit_test.c
   ```

2. Add the component to `source/components/Kconfig`.
3. Register unit and smoke tests via CTest in `tests/components/<name>/CMakeLists.txt`,
   labelled `actrust` plus `component` and either `unit` or `smoke`.
4. Document the public API in the component header with Doxygen.
5. Keep the dependency order (high to low) intact:

   ```
   cloud -> mqtt, crypto, json, kv, tls, queue, log, adapter
   mqtt  -> tls, queue, log, crypto, coreMQTT-Agent, backoffAlgorithm, adapter
   ...
   ```

---

## Commit Message Style

Format: `[verb] description`, where verb is one of: `add`, `update`, `fix`, `format`.

Examples:

```
[add] kv: persistent key-value backed by storage adapter
[fix] core: ensure payload is null-terminated in core_copy_send_payload
[update] core: enhance initialization with claim certificate and key support
[format] tls: re-run formatter after rebase
```

Keep the subject line under 72 characters. Use the commit body for the *why*
and any background, not the *what*.

---

## Pull Request Checklist

- [ ] `tools/format.sh` produces no diff.
- [ ] `./build.sh linux_x86 --clean-build --test` passes locally.
- [ ] New public APIs are documented with Doxygen.
- [ ] New files carry the SPDX header.
- [ ] Commit messages follow the `[verb] description` style.
- [ ] No changes to `3rdparts/` (unless updating a submodule SHA on purpose).
- [ ] No secrets, real device certificates, or credentials are committed.
