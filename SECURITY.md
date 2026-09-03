# Security Policy

We take the security of AntChainTrustSDK seriously. This document describes how to
report a vulnerability and what to expect from the maintainers.

## Supported Versions

| Version       | Supported          |
| ------------- | ------------------ |
| `1.1.x`       | :white_check_mark: |
| `1.0.x`       | :white_check_mark: |
| `< 1.0`       | :x:                |

Security fixes are released from the applicable stable maintenance line. The
`release/v1.1.x` branch carries compatible patch fixes for the `v1.1` line;
new public capabilities require a minor release and incompatible changes
require a new major release.

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public GitHub issues,
pull requests, or discussions.**

Instead, please report them privately by emailing:

> **zhongzhehua.zzh@antgroup.com**

Include, where possible:

- A description of the issue and its impact.
- The affected component(s) (e.g., `tls`, `crypto`, `adapter/platform/linux`)
  and version / commit SHA.
- Steps to reproduce or a minimal proof-of-concept.
- Any mitigations or workarounds you have identified.
- Your name / handle for credit in the advisory (optional).

You should receive an acknowledgement within **2 business days**. We aim to
provide a triage assessment within **7 business days** and a remediation plan
within **30 days** for high/critical findings.

## Disclosure Process

1. You report the issue via email to the address above.
2. We confirm receipt, investigate, and validate the finding.
3. We prepare a fix and, where applicable, a CVE assignment.
4. We coordinate a disclosure date with the reporter.
5. The fix is released and a public advisory is published in the repository's
   GitHub Security Advisories section.

We follow a **coordinated disclosure** model. We will not publish details of
the vulnerability before a fix is available, and we kindly ask reporters to
do the same.

## Scope

This policy covers the source under this repository, **excluding** vendored
third-party code under `3rdparts/`. Vulnerabilities in those projects should
be reported to their respective upstreams:

- mbedTLS -- https://www.trustedfirmware.org/security/
- coreMQTT / coreMQTT-Agent / coreJSON / coreSNTP / backoffAlgorithm --
  https://github.com/FreeRTOS

If a vulnerability in a third-party component is exposed by how AntChainTrustSDK
uses it, it is in scope.

## Out of Scope

- Issues that require physical access to the device.
- Denial-of-service that requires root privileges on the device.
- Reports generated solely by automated scanners with no demonstrable impact.
- Vulnerabilities in deployments that have disabled certificate verification
  or other security features against the documented guidance.
