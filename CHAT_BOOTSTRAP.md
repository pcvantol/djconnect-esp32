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
- Laatste geverifieerde release is v3.2.0.
- v3.2.0 publiceert alleen het LilyGO T-Embed S3 firmware asset:
  `djconnect-lilygo-t-embed-s3-v3.2.0.bin`, `.sha256` en
  `firmware_manifest.json`.
- De ESP32-S3-BOX-3 PlatformIO/release/CI target is verwijderd; niet opnieuw
  toevoegen tenzij de gebruiker expliciet nieuwe board support vraagt. Ook de
  resterende inactive BOX-3 board-profile code is verwijderd.
- Community/security-documentatie en security-contact blijven actueel:
  `security@djconnect.dev`.
- GitHub repo hygiene: secret scanning, push protection, Dependabot alerts en
  branch protection op `main` staan aan. Voor maintainer-run releases kan admin
  enforcement kort gecontroleerd uit, release push, en direct weer aan.
- Publieke OTA assets staan in `pcvantol/djconnect-firmware`. Als de source
  GitHub Action niet publiceert of expliciete lokale publicatie gewenst is,
  gebruik `./release.sh X.Y.Z --publish-firmware-repo ../djconnect-firmware`
  en maak/verifieer daarna de public firmware GitHub release met alleen de
  LilyGO `.bin`, `.sha256` en manifest assets.
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
