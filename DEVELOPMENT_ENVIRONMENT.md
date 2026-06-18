# Development Environment

This document describes the local development setup for DJConnect ESP32
firmware.

## Supported Target

The supported PlatformIO firmware environment is:

```sh
t_embed_cc1101
```

This target builds the LilyGO T-Embed-CC1101 / T-Embed S3 firmware. The project
uses the pinned pioarduino ESP32 platform line from `platformio.ini`, based on
ESP-IDF 5.3 and Arduino ESP32 3.x compatibility. Arduino ESP32 2.x / ESP-IDF
4.x fallback code is not maintained.

## Required Tools

- PlatformIO, normally installed at
  `/Users/pcvantol/.platformio/penv/bin/pio` on the maintainer machine.
- A C++17 compiler for native host tests.
- Python 3 for web portal minification and release hygiene checks.
- GitHub CLI (`gh`) for release and workflow maintenance.
- A USB-connected LilyGO T-Embed-CC1101 for flashing and hardware validation.

## First-Time Setup

From the repository root:

```sh
/Users/pcvantol/.platformio/penv/bin/pio pkg install -e t_embed_cc1101
```

If the local PlatformIO executable is not at that path, use `pio` from the
active shell environment or set `PIO_BIN` when running release tooling.

Do not add WiFi credentials, Home Assistant tokens, OAuth credentials or private
URLs to source files. `include/Secrets.h` is only for optional non-secret build
flags.

## Common Commands

Run native host tests when changing pure logic:

```sh
c++ -std=c++17 -Iinclude -I.pio/libdeps/t_embed_cc1101/ArduinoJson/src test/native/test_logic.cpp -o /tmp/djconnect_unit_tests
/tmp/djconnect_unit_tests
```

Run release-script and repository hygiene checks when touching release tooling,
docs hygiene, asset names or release contracts:

```sh
bash test/native/test_release.sh
```

Build firmware:

```sh
/Users/pcvantol/.platformio/penv/bin/pio run -e t_embed_cc1101
```

Build with explicit release version flags:

```sh
DJCONNECT_BUILD_FLAGS='-DDJCONNECT_VERSION=X.Y.Z -DDJCONNECT_VERSION_TAG=vX.Y.Z' \
/Users/pcvantol/.platformio/penv/bin/pio run -e t_embed_cc1101
```

Upload to a USB-connected LilyGO:

```sh
/Users/pcvantol/.platformio/penv/bin/pio device list
/Users/pcvantol/.platformio/penv/bin/pio run -e t_embed_cc1101 -t upload --upload-port /dev/cu.usbmodemXXXX
```

Replace `/dev/cu.usbmodemXXXX` with the serial port shown by PlatformIO.

## Web Portal Changes

The embedded web portal lives in `src/WebPortal.cpp`. After changing portal
markup, CSS or JavaScript, regenerate the compact embedded asset:

```sh
python3 scripts/minify_webportal.py
```

Do not add runtime decompression requirements for the no-PSRAM LilyGO path.

## Release Preparation

Dry-run a release before publishing:

```sh
./release.sh X.Y.Z --dry-run
```

Create a release:

```sh
./release.sh X.Y.Z
```

Release builds publish only the LilyGO asset:

```text
djconnect-lilygo-t-embed-s3-vX.Y.Z.bin
djconnect-lilygo-t-embed-s3-vX.Y.Z.bin.sha256
firmware_manifest.json
```

Release tooling updates PlatformIO Core and project packages for
`t_embed_cc1101`, writes dependency reports under `release/`, builds firmware,
creates the source tag and pushes it. Public OTA assets live in
`pcvantol/djconnect-firmware`; maintainer-run releases should use
`--publish-firmware-repo ../djconnect-firmware` and then verify or create the
public GitHub firmware release with only the LilyGO binary, its `.sha256` file
and `firmware_manifest.json`. If the source GitHub Actions release workflow is
used, verify that it published the same LilyGO-only asset set.

Before publishing, keep `README.md`, `CHANGELOG.md`, `HANDOFF.md`,
`CHAT_BOOTSTRAP.md`, `DESIGN_DECISIONS.md`, `THIRD_PARTY_NOTICES.md` and tests
current when relevant.

## Generated Files

Generated firmware binaries, manifests and build logs are ignored by git:

- `.pio/`
- `release/`
- `*.bin`
- `*.elf`
- `*.map`

Clean local build and release output when it is no longer needed:

```sh
rm -rf .pio/build .pio/build-release release
```

Keep `.pio/libdeps/t_embed_cc1101` when you want native host tests to compile
against the locally installed ArduinoJson dependency.

## Secrets And AI-Assisted Development

Do not put secrets, private data or proprietary third-party material in:

- source files;
- docs;
- logs;
- diagnostics;
- screenshots;
- issues;
- test fixtures;
- prompts or agent logs.

DJConnect may use AI-assisted and agentic engineering workflows, including
Codex. All accepted changes remain maintainer-reviewed and must be testable,
license-compatible and free of secrets.

Security-sensitive reports should be sent privately to `security@djconnect.dev`
instead of opened as public GitHub issues.

## Expected Warnings

ESP-IDF 5.x legacy I2S warnings from `ESP8266Audio` and the microphone recorder
are expected during current builds. They are not normally actionable until the
speaker and microphone paths migrate from the legacy I2S API to `i2s_std` /
`i2s_pdm`.
