# Contributing to DJConnect

Thanks for helping improve DJConnect.

This repository is part of the DJConnect project and is MIT-licensed. Related
DJConnect repositories are also MIT-licensed unless their own repository metadata
or a third-party dependency states otherwise. See [LICENSE](LICENSE) for the
license that applies to this repository.

## What Belongs Here

This repository contains the DJConnect ESP32-S3 firmware for the LilyGO
T-Embed-CC1101. Firmware source,
embedded web portal code, device UI behavior, Home Assistant device-layer
support, docs, tests, build scripts and release workflow updates belong here.

Do not commit secrets, tokens, WiFi passwords, OAuth credentials, Home Assistant
device tokens, private URLs or private user data. Keep those values out of
source files, example configs, logs, diagnostics and screenshots.

## Development Setup

Use PlatformIO with the pinned ESP32 platform from `platformio.ini`. The
supported firmware environment is `t_embed_cc1101`. See
[`DEVELOPMENT_ENVIRONMENT.md`](DEVELOPMENT_ENVIRONMENT.md) for the full local
setup, build, upload and release workflow.

Run native tests when changing pure logic:

```sh
c++ -std=c++17 -Iinclude -I.pio/libdeps/t_embed_cc1101/ArduinoJson/src test/native/test_logic.cpp -o /tmp/djconnect_unit_tests
/tmp/djconnect_unit_tests
```

Run release-script checks when touching release tooling:

```sh
bash test/native/test_release.sh
```

Build the firmware target:

```sh
/Users/pcvantol/.platformio/penv/bin/pio run -e t_embed_cc1101
```

After web portal markup, CSS or JavaScript changes, regenerate the embedded
portal:

```sh
python3 scripts/minify_webportal.py
```

Dry-run a release before publishing:

```sh
./release.sh X.Y.Z --dry-run
```

## Cross-Repo Contract

Changes to protocols, endpoints, client types, pairing, OTA, Assist/STT/TTS,
Spotify playback, branding or shared user-facing terminology must be coordinated
with the rest of DJConnect.

Coordinate Home Assistant integration changes with `pcvantol/djconnect`.
Coordinate client-visible behavior with the relevant app, firmware or web
client repositories. If a change affects shared contracts or roadmap scope,
update `SYNC_PROMPTS.md` and `PRODUCT_ROADMAP.md` in `pcvantol/djconnect`.

## Contribution Guidelines

- Keep changes small and focused.
- Add tests for code or contract changes.
- Update docs and examples for user-facing or protocol changes.
- Do not log secrets.
- Respect the Spotify trademark and non-affiliation notice: DJConnect is not
  connected to, approved by or sponsored by Spotify AB.
- Use the real DJConnect brand assets; do not redraw the logo or icon unless it
  is being intentionally replaced.

## AI-Assisted Development

DJConnect is developed and maintained with AI-assisted and agentic engineering
workflows, including Codex. AI assistance may be used for code changes,
documentation, tests, release preparation and cross-repo consistency checks.

All accepted changes remain maintainer-reviewed. Contributors are responsible
for ensuring their changes are correct, testable, license-compatible and free of
secrets or private data. Do not include tokens, passwords, private URLs,
personal data or proprietary third-party material in prompts, issues, logs,
screenshots or test fixtures.

## Pull Requests

Before opening a pull request:

1. Run the repo-specific tests and builds that match your change.
2. Check `git status`.
3. Describe what changed.
4. List the checks you ran.
5. Note the impact on other DJConnect repositories.

## Releases

Firmware releases use `./release.sh X.Y.Z` from the repository root. Versions
and tags use semantic versioning with a `vX.Y.Z` Git tag. Always run
`./release.sh X.Y.Z --dry-run` first.

The release script builds the LilyGO T-Embed-CC1101 firmware asset, writes the
firmware manifest, commits the source release, creates the tag and pushes it.
Public OTA artifacts are published through `pcvantol/djconnect-firmware`; for
maintainer-run releases, use `--publish-firmware-repo ../djconnect-firmware`
and verify or create the public GitHub firmware release with only the LilyGO
binary, its `.sha256` file and `firmware_manifest.json`. If the GitHub Actions
release workflow is used, verify that it published the same LilyGO-only asset
set.

After a release, keep related DJConnect repositories synchronized when firmware
behavior changes shared contracts, setup flows, OTA metadata, Assist behavior,
Spotify playback behavior or branding.

## Licensing

By contributing to this repository, you agree that your contribution is licensed
under the MIT License in `LICENSE`.

Spotify is a trademark of Spotify AB. DJConnect is not affiliated with, endorsed
by, or sponsored by Spotify AB.
