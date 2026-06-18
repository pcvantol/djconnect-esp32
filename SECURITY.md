# Security Policy

## Reporting a Vulnerability

If you believe you have found a security vulnerability in DJConnect firmware,
please report it privately by email:

```text
security@djconnect.dev
```

Please do not open a public GitHub issue for security-sensitive reports.

When possible, include:

- a clear description of the issue;
- affected firmware version, board target and commit if known;
- steps to reproduce;
- potential impact;
- any relevant logs, screenshots or proof-of-concept details that do not expose
  secrets or private systems.

Avoid sending real WiFi passwords, Home Assistant tokens, OAuth credentials,
private URLs or other secrets. Redacted examples are preferred.

## Supported Versions

Security fixes are normally made against the current active firmware line. Older
local development builds, prerelease experiments and unpublished forks may not
receive separate fixes.

## Coordinated Disclosure

The maintainer will review security reports and may ask for additional
information. Please give the project reasonable time to investigate and prepare
a fix before sharing details publicly.

If the vulnerability affects another DJConnect repository, Home Assistant
integration behavior or third-party dependency, the fix may require coordinated
updates across repositories or upstream projects.

## Scope

Security reports for this repository should focus on the ESP32 firmware, local
device API, provisioning behavior, OTA behavior, credential handling, logs,
diagnostics and repository release tooling.

Reports about unrelated third-party services or platforms should be sent to the
appropriate project or vendor.
