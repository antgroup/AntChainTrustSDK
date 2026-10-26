# Changelog

All notable changes to AntChainTrustSDK are documented here.

## [Unreleased]

## [1.1.1] - 2026-10-26

### Fixed

- Release baseline selection, complete multiline public declaration checks,
  compatibility regression tests with initialized Git fixtures, and use of
  current release tools when validating historical tags (#2).
- Signing-key cleanup and recovery after failed core initialization (#3).
- Preservation of AWS runtime identity on credential access failures (#4).
- KV v2 configuration limits and serialized record sizes (#5).

### Added

- Bug report, feature request, and pull request templates, README resource
  navigation, and CI and release badges (#6).
- Evidence-linked validation results and known limitations for the published
  v1.1.0 release (#6).
- ECDSA P-256 release signing, a published verification key, and bundle
  verification instructions.

### Compatibility

- Public API and ABI compatibility is checked against v1.1.0.
- KV v2 now rejects configurations where `CONFIG_ACTRUST_KV_MAX_KEY_LEN`
  is not 32, matching its existing persisted key layout.

## [1.1.0] - 2026-09-02

### Added

- Release automation with tag, API, ABI, package, checksum, and SBOM gates.
- Versioned API documentation and an auditable release note.
- A release signing hook that delegates key handling to the CI environment.
- Installable CMake package and Linux x86 SDK bundle with dependency metadata,
  license notices, an SPDX SBOM, and SHA-256 checksum generation.
- Linux Debug/Release and sanitizer checks, plus Linux ARM and Android
  arm64-v8a cross-build workflows.

### Changed

- The SDK version is read from the repository `VERSION` file.
- Public API contracts, platform adapters, asynchronous lifecycle handling,
  and KV v2 persistence were updated after the v1.0.0 baseline.

### Validation and limitations

- Linux host tests, format/Doxygen checks, and Linux ARM and Android builds
  passed for the release commit; see the [release notes][1.1.0] for evidence
  and configurations.
- No validated binary assets are attached to the published GitHub release.
  Device, AWS/PKI, and signing validation require external resources.
- Issues #2–#5 were identified after tagging; their fixes are listed under
  v1.1.1 and are not part of the immutable v1.1.0 source tag.

[Unreleased]: https://github.com/antgroup/AntChainTrustSDK/compare/v1.1.1...develop
[1.1.1]: docs/releases/v1.1.1.md
[1.1.0]: docs/releases/v1.1.0.md
