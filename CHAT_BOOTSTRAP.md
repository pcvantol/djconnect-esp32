# Chat Bootstrap Prompt

Use this prompt to initialize a fresh Codex chat for this repository:

```text
Werk in repo: /Users/pcvantol/Documents/GitHub/djconnect-esp32

Lees eerst:
- AGENTS.md
- HANDOFF.md
- README.md
- CHANGELOG.md
- CONTRIBUTING.md

Context:
- DJConnect ESP32-S3 firmware repo.
- Huidige licentie is MIT.
- Laatste release is v3.1.31, commit 5865621.
- v3.1.31 verwijdert de ESP32-S3-BOX-3 PlatformIO/release/CI target en
  publiceert alleen het LilyGO T-Embed S3 firmware asset.
- v3.1.31 bevat community/security-documentatie en security-contact
  `security@djconnect.dev`.
- Werkmap zou schoon moeten zijn; controleer met `git status --short`.
- Houd cross-repo contracten met `pcvantol/djconnect` actueel als protocol, HA integration, OTA, Assist/STT/TTS, Spotify playback, branding of roadmap geraakt wordt.

Belangrijke regels:
- Gebruik `apply_patch` voor handmatige edits.
- DJConnect wordt ontwikkeld en onderhouden met AI-assisted/agentic engineering workflows, inclusief Codex; accepted changes blijven maintainer-reviewed en prompts/logs/issues mogen geen secrets of private data bevatten.
- Geen secrets/tokens/wachtwoorden in code, docs, logs of diagnostics.
- WebPortal markup/CSS/JS gewijzigd? Draai `python3 scripts/minify_webportal.py`.
- Native tests bij pure logic changes:
  `c++ -std=c++17 -Iinclude -I.pio/libdeps/t_embed_cc1101/ArduinoJson/src test/native/test_logic.cpp -o /tmp/djconnect_unit_tests`
  `/tmp/djconnect_unit_tests`
- Release tooling geraakt? Draai `bash test/native/test_release.sh`.
- Firmware build:
  `/Users/pcvantol/.platformio/penv/bin/pio run -e t_embed_cc1101`
- Release dry-run:
  `./release.sh X.Y.Z --dry-run`
- Release:
  `./release.sh X.Y.Z`

Start met `git status --short` en bevestig kort waar we staan.
```
