# Technical Design Decisions

DJConnect firmware is MIT-licensed embedded firmware for ESP32-S3 devices. This
document records the design decisions that can be deduced from the codebase,
with source references to the files that currently implement or document those
decisions.

## Scope And Sources

This document is reverse-engineered from the repository state and should be kept
current with each release. It complements, but does not replace, `README.md`,
`AGENTS.md`, `THIRD_PARTY_NOTICES.md`, `platformio.ini` and the source tree.

Primary sources used:

- `platformio.ini` for firmware environments, build flags and direct library
  dependencies.
- `AGENTS.md` for explicit architectural, coding and release rules for coding
  agents.
- `README.md` for user-facing architecture, setup and API behavior.
- `THIRD_PARTY_NOTICES.md` and installed PlatformIO package metadata for
  dependency attribution.
- `include/*.h` and `src/*.cpp` for module boundaries and implementation
  patterns.
- `test/native/test_logic.cpp` and `test/native/test_release.sh` for testable
  design contracts.

## Language And Runtime Split

| Language / format | Role | Main files |
| --- | --- | --- |
| C++17 / Arduino C++ | Firmware runtime, device logic, rendering, HTTP APIs, audio, OTA and hardware control | `include/`, `src/`, `test/native/test_logic.cpp` |
| Bash | Release and hygiene automation | `release.sh`, `scripts/cleanup_old_releases.sh`, `test/native/test_release.sh` |
| Python | PlatformIO upload/monitor helper hook | `scripts/platformio_upload_monitor_delay.py` |
| HTML / CSS / JavaScript | Embedded captive portal and web portal served from firmware | `src/WebPortal.cpp`, captive portal HTML in `src/DJConnectApp.cpp` |
| JSON | Postman collections, web manifest, model metadata and release manifest output | `postman/`, `assets/website/site.webmanifest`, `third_party/micro_wake_word/okay_nabu.json` |
| RGB565 / binary assets | Embedded display assets and wake-word model asset | `assets/esp_display/embedded/`, `third_party/micro_wake_word/okay_nabu.tflite` |

## Code-Level Design Patterns

### Application Orchestrator

`DJConnectApp` is the top-level application orchestrator. It owns the main
startup and loop flow, composes subsystem instances and routes physical input,
network polling, voice, display, power and Home Assistant state transitions.

Sources:

- `include/DJConnectApp.h`
- `src/DJConnectApp.cpp`
- `AGENTS.md` Architecture Rules

Why:

- Arduino firmware has one `setup()` / `loop()` entrypoint. Keeping `main.cpp`
  tiny and moving real behavior into `DJConnectApp` keeps the firmware testable
  and avoids hidden global control flow.
- Long-lived embedded state is explicit in one owner while specialized behavior
  remains delegated to focused modules.

### Module Ownership / Facade Boundaries

Subsystem classes own concrete responsibilities:

- `DisplayManager`: TFT drawing, backlight and screenshot framebuffer access.
- `WebPortal`: mobile web UI and shared port-80 `WebServer`.
- `DJConnectApiServer`: local protected device API routes for Home Assistant and
  debug/device control.
- `SpotifyClient`: backend-agnostic playback proxy HTTP calls to Home Assistant.
- `ProvisioningController`: NVS provisioning/settings storage.
- `PowerController`: charger, deep sleep and watchdog policy. Normal idle
  turn-off sleep is battery-only; USB-C/external power suppresses it while still
  allowing screen dim/off behavior.
- `LedRing`: WS2812 ring presentation.
- `VoiceRecorder`, `VoiceHttpClient`, `DjResponseAudioPlayer`, `WakeWordEngine`:
  voice and wake-word paths.

Sources:

- `include/DisplayManager.h`, `src/DisplayManager.cpp`
- `include/WebPortal.h`, `src/WebPortal.cpp`
- `src/DJConnectApiServer.h`, `src/DJConnectApiServer.cpp`
- `include/SpotifyClient.h`, `src/SpotifyClient.cpp`
- `include/ProvisioningController.h`, `src/ProvisioningController.cpp`
- `include/PowerController.h`, `src/PowerController.cpp`
- `include/LedRing.h`, `src/LedRing.cpp`
- `include/VoiceRecorder.h`, `src/VoiceRecorder.cpp`
- `include/WakeWordEngine.h`, `src/WakeWordEngine.cpp`

Why:

- Hardware firmware is easier to reason about when every side effect has a
  single owner. For example, rendering never writes NVS, and provisioning never
  draws screens.
- It reduces accidental coupling between UI, network and power paths, which is
  important for watchdog stability.

### Board Profile / Compile-Time Capability Pattern

`BoardProfile.h` selects board-specific pins, device model names and hardware
capability flags at compile time. `Config.h` lifts the active profile into the
rest of the firmware.

Sources:

- `include/BoardProfile.h`
- `include/Config.h`
- `platformio.ini` environments `t_embed_cc1101` and `esp32_s3_box3`

Why:

- The LilyGO T-Embed S3 and ESP32-S3-BOX-3 share the app logic but differ in
  display, buttons, battery and LED-ring hardware.
- Compile-time capability flags keep unsupported hardware paths out of runtime
  behavior without adding legacy fallback formats or generic device IDs.

### Explicit State Snapshot Pattern

UI and status paths consume state structs rather than ad hoc globals. Examples
include playback, battery, runtime diagnostics, visual state and notices.

Sources:

- `include/AppState.h`
- `src/DJConnectApp.cpp`
- `src/WebPortal.cpp`
- `src/DJConnectApiServer.cpp`

Why:

- The same state must feed the TFT, LED ring, web portal and Home Assistant
  status payload. Snapshot structs reduce drift between these outputs.
- Small value-like structs are easier to serialize and test than scattered state.
- The queue snapshot accepts up to 100 real queue items from Home Assistant,
  matching the shared playback contract while still bounding device/web JSON
  response size. Playlist browsing remains capped separately at 20 items on
  ESP32 clients for responsiveness.

### Pure Logic Extraction For Host Tests

Calculations that do not need Arduino hardware live in headers such as
`LogicHelpers.h`, `DJConnectMenuModel.h`, `DeviceCommandParser.h` and
`NetworkActivityLogic.h`, with native C++ tests.

Sources:

- `include/LogicHelpers.h`
- `include/DJConnectMenuModel.h`
- `include/DeviceCommandParser.h`
- `include/NetworkActivityLogic.h`
- `test/native/test_logic.cpp`

Why:

- Most firmware behavior is hardware-bound, but pure rules such as semver,
  menu counts, battery estimates and parser behavior can be tested quickly on
  the host.
- This creates regression coverage without requiring a connected ESP32-S3.

### Callback Adapter Pattern

`WebPortal`, `DJConnectApiServer`, `WakeWordEngine`, `SoftResetMonitor` and
related modules use callback pointers plus a context pointer to call back into
`DJConnectApp`.

Sources:

- `include/WebPortal.h`
- `src/DJConnectApiServer.h`
- `include/DJConnectApp.h`
- `src/DJConnectApp.cpp`

Why:

- Avoids global singleton dependencies while still fitting Arduino-style object
  lifetimes.
- Keeps lower-level modules reusable and testable because they do not need to
  include all of `DJConnectApp`.

### Protected Local API Pattern

Open local endpoints are limited to device info and pairing info. Protected
endpoints require `Authorization: Bearer <device_token>`.

Sources:

- `src/DJConnectApiServer.cpp`
- `README.md` Local Device API
- `AGENTS.md` Home Assistant Device Layer

Why:

- Pairing and discovery must be possible before auth, but reboot, OTA, pairing
  reset, commands, DJ responses and screenshots reveal/control device state and
  therefore require the paired device token.

### Setup-Only mDNS Discovery Pattern

`DJConnectDiscovery` advertises `_djconnect._tcp` only while the ESP is unpaired.
Once Home Assistant pairing is stored, the firmware stops ESPmDNS advertising;
paired runtime traffic uses the stored LAN `ha_local_url` and authenticated local
ESP endpoints instead of continuing to appear as a discoverable setup candidate.
Resetting pairing or returning to setup/pairing mode starts discovery again.

Sources:

- `src/DJConnectDiscovery.cpp`
- `src/DJConnectApp.cpp`
- `README.md` mDNS Discovery

Why:

- Keeps Home Assistant discovery focused on devices that still need pairing.
- Avoids already-paired devices repeatedly appearing as new discovery candidates.

### HA Boundary / Backend-Agnostic Playback Pattern

The ESP never stores Spotify OAuth or backend credentials. It sends generic
commands to Home Assistant and receives generic playback state back.

Queue and playlist commands carry explicit positive integer limits at the client
boundary. ESP32 queue requests use `limit=100`; ESP32 playlist requests use
`limit=20` to preserve device responsiveness while staying below Spotify's
playlist API maximum. Other clients may request up to `limit=50` for playlists.
Home Assistant owns backwards-compatible defaulting and clamping when older
clients omit `limit`, and should keep provider-specific errors out of
user-facing state.

Queue item playback treats `context_uri`/`queue_context` as optional. Items with
a direct Spotify track or episode URI remain startable without context; playlist,
album and show contexts keep the existing context-plus-offset payload when Home
Assistant provides them.

New client setup/settings flows do not expose or expect legacy Home Assistant
playback source/default-playlist override options. Source, device and
default-playlist choices belong in the Home Assistant integration, and
user-facing setup copy labels the client URL as `Client adres`.

Home Assistant `v3.1.z` and ESP `v3.1.z` are matched by major/minor version, not
by patch. HTTP 426 with `error:"version_mismatch"` is treated as an update
requirement and does not clear the stored pairing token. Backend availability is
also runtime state: `backend_available:false` maps to a localized Home Assistant
Spotify-connection hint, while 401/403/404 mark pairing stale.

Sources:

- `README.md` Playback Backend and Home Assistant Integration
- `AGENTS.md` Architecture Decisions
- `src/SpotifyClient.cpp`

Why:

- Keeps backend secrets off the ESP.
- Allows Spotify today and other backends later without rebuilding the firmware
  around provider-specific APIs.

### Non-Blocking / Watchdog-Aware Operation

Long network, OTA, audio and voice flows use explicit timeouts, yielding,
`NetworkActivity`, watchdog feeding or scoped watchdog pauses.

Sources:

- `include/NetworkActivity.h`, `src/NetworkActivity.cpp`
- `include/ScopedWatchdogPause.h`
- `src/DJConnectOTA.cpp`
- `src/DJConnectApp.cpp`
- `src/SpotifyClient.cpp`

Why:

- ESP32-S3 UI, audio, microphone, HTTP and wake-word inference share limited
  heap and CPU time. Blocking calls can freeze input or trip the task watchdog.
- The code prefers controlled failure and diagnostic logs over indefinite waits.
- Wake-word inference defaults to off and is a persisted user setting because
  its TFLite arena competes with TLS, album-art and audio flows on no-PSRAM
  boards. Enabling it is explicit from device settings, web settings or Home
  Assistant after pairing.

### Render-To-Sprite / Framebuffer Pattern

The TFT path renders to a `TFT_eSprite` when available and pushes the full frame
to the display. The authenticated screenshot endpoint streams the latest frame
as BMP row-by-row.

Sources:

- `include/DisplayManager.h`
- `src/DisplayManager.cpp`
- `src/DJConnectApiServer.cpp`

Why:

- Sprite rendering reduces visible flicker on redraws.
- Row-by-row BMP streaming avoids allocating a full screenshot file in RAM,
  which matters on non-PSRAM LilyGO boards.
- Release firmware keeps screenshots protected by the paired device token. Local
  `dev` / `vdev` firmware exposes the screenshot endpoint without auth to support
  development capture after USB deploys without retrieving runtime secrets.
- The `POST /api/device/debug/screen` route follows the same policy and exists
  only to make automated visual capture deterministic: tooling opens one known
  UI screen, captures BMP, converts to PNG, and repeats.

### State Guards For Modal Device Modes

Setup, WiFi failure, HA pairing, low battery, critical battery, OTA, games,
voice processing and DJ announcement screens are treated as modal states that
block normal playback/menu input.

The ESP treats `audio_url` as the sole playback contract for DJ-announcement
audio returned by Home Assistant. Text-only responses remain valid and are
displayed, but `audio_url=none` is deliberately not retried as speaker playback;
that indicates the Home Assistant voice/TTS path did not provide audio.

Sources:

- `src/DJConnectApp.cpp`
- `src/VoiceHttpClient.cpp`
- `src/DjResponseAudioPlayer.cpp`
- `README.md` setup, pairing, battery and voice sections

Why:

- Embedded devices need clear priority rules. Low battery and OTA must trump
  normal UI; pairing/setup must keep networking alive but suppress playback
  actions; DJ-announcement cancellation must ignore late HTTP results.

### Wake-Only Splash Pattern

When the display wakes from backlight-off normal mode, the first physical input
is consumed by a short DJConnect splash and then the active UI is restored.
Pairing and low-battery guard screens keep priority and do not get replaced by
the normal-mode splash.

Sources:

- `src/DJConnectApp.cpp`
- `include/Config.h`
- `include/DisplayManager.h`
- `src/DisplayManager.cpp`

Why:

- The first input after screen-off should wake the device visually without
  accidentally triggering playback/menu/game actions.
- Showing the brand splash at wake gives a consistent "screen turned on" moment
  while preserving stronger modal safety screens.

### Release Contract / Artifact Split Pattern

Release builds inject `DJCONNECT_VERSION` and `DJCONNECT_VERSION_TAG`, build both
board environments and publish board-specific firmware assets plus a manifest
with a `firmwares` array.

Before release firmware is built, `scripts/update_build_dependencies.sh` upgrades
PlatformIO Core and updates global/project PlatformIO packages for the selected
board environments. The script writes before/after package lists and a diff into
the release artifact directory so dependency version changes are visible during
release review.

Public GitHub release notes are extracted from the matching `CHANGELOG.md`
release section through `scripts/extract_release_changelog.sh`; missing or empty
release sections fail the release-note preparation instead of publishing a
generic body.

Release builds also pass `DJCONNECT_RELEASE_BUILD=1` and `-Os` through
`DJCONNECT_BUILD_FLAGS`. The embedded web portal remains a C++ PROGMEM raw
literal for zero-runtime-dependency serving, but `scripts/minify_webportal.py`
removes indentation-heavy HTML/CSS/JS whitespace after portal edits. JavaScript
newlines are intentionally preserved by the minifier to avoid changing browser
automatic-semicolon-insertion behavior.
Link-time optimization is intentionally disabled for now: the current Arduino
ESP32 / ESP-IDF 5.3 toolchain emits LTO objects but fails the final link with
missing linker-plugin handling and an `app_main` reference error.

Sources:

- `release.sh`
- `scripts/minify_webportal.py`
- `scripts/extract_release_changelog.sh`
- `test/native/test_release.sh`
- `platformio.ini`
- `README.md` OTA sections

Why:

- Source defaults stay `dev` / `vdev`, while release artifacts carry immutable
  semver.
- Board-specific assets avoid ambiguous `djconnect-device` binaries and let HA
  select firmware by device model.
- Updating build dependencies before release reduces stale framework/tool risk,
  while the generated dependency diff makes third-party notice and dependency
  inventory updates explicit instead of relying on memory.
- Reusing the changelog section for GitHub Releases keeps public release notes,
  OTA release metadata and repository history aligned.

## Coding Style And Conventions

### C++ / Arduino Firmware

Observed conventions:

- Header files use `#pragma once`.
- Classes use `PascalCase`; methods and variables use `camelCase`; private
  members use a trailing underscore, for example `display_`.
- Constants and compile-time values use `PascalCase` in namespaces/classes,
  for example `Config::WifiConnectTimeoutMs`.
- Enum types use `PascalCase`; enum values are descriptive, e.g. `UiScreen`.
- Source files begin with short purpose comments when useful.
- Comments are concise and explain ownership, constraints or non-obvious
  embedded behavior.
- `String` is used for Arduino-facing text and JSON-bound values; fixed C
  strings and `constexpr` are used for constants.
- Logging goes through `AppLog` and central severity formatting; callsites do
  not manually prepend timestamps/severity.
- Host-testable logic is kept free of Arduino dependencies where practical.
- Runtime behavior is intentionally English in logs; user-facing strings flow
  through `I18n` where practical.

Sources:

- `AGENTS.md` Architecture Rules, Display And UI Rules, Home Assistant Device
  Layer and Editing constraints.
- `include/*.h`, `src/*.cpp`.
- `src/AppLog.cpp`, `src/I18n.cpp`.

### Bash

Observed conventions:

- Scripts use `#!/usr/bin/env bash` and `set -euo pipefail`.
- Helper functions are small and named with `snake_case`, for example
  `default_min_ha_integration_for`.
- Release scripts echo planned commands and support `--dry-run`.
- Shell tests assert behavior with explicit `grep` checks.

Sources:

- `release.sh`
- `scripts/cleanup_old_releases.sh`
- `test/native/test_release.sh`

### Python

Observed conventions:

- Python is used narrowly for PlatformIO hook behavior.
- It avoids owning firmware logic; firmware behavior remains in C++.

Sources:

- `scripts/platformio_upload_monitor_delay.py`
- `platformio.ini` `extra_scripts`

### HTML / CSS / JavaScript

Observed conventions:

- The embedded web portal is generated from C++ raw string literals and served
  by `WebPortal`.
- CSS uses a dark DJConnect blue/purple theme with semantic status colors.
- Browser-side games and UI state remain local to the browser unless explicitly
  routed through ESP endpoints.
- Web controls avoid page-level reloads and use `fetch()` against local ESP
  endpoints.

Sources:

- `src/WebPortal.cpp`
- `src/DJConnectApp.cpp` captive portal page.

### JSON

Observed conventions:

- ESP-to-HA JSON payloads include top-level `device_id` and
  `client_type:"esp32"`.
- ESP-to-HA payloads do not use `device_type`.
- Device API and Postman examples use placeholders for secrets.
- Release manifests use a board-specific `firmwares` array.

Sources:

- `src/DJConnectApiServer.cpp`
- `src/SpotifyClient.cpp`
- `postman/*.postman_collection.json`
- `release.sh`
- `AGENTS.md`

## Frameworks, Libraries And Third-Party Dependencies

Versions are taken from `platformio.ini`, PlatformIO build output and installed
package metadata where available. Licenses are taken from package metadata or
license files in `.pio/libdeps/t_embed_cc1101` / PlatformIO packages. Direct
firmware dependencies are linked unless marked otherwise.

### Firmware Runtime And Platform

| Component | Version | License | Source URL | Use |
| --- | --- | --- | --- | --- |
| pioarduino Espressif 32 platform | `53.03.11` package URL | Upstream platform package; component licenses apply | `https://github.com/pioarduino/platform-espressif32` | PlatformIO ESP32-S3 platform pin with ESP-IDF 5.3 / Arduino ESP32 3.x |
| Arduino core for ESP32 | `3.1.1` | LGPL-2.1-or-later | `https://github.com/espressif/arduino-esp32` | Arduino runtime, WiFi, BLE, WebServer, Preferences, LittleFS, Update, ESPmDNS |
| Arduino ESP32 precompiled libs / ESP-IDF libs | `5.3.0+sha.cfea4f7c98` | LGPL-2.1-or-later in package metadata; ESP-IDF component licenses apply | `https://github.com/espressif/esp32-arduino-lib-builder` | ESP-IDF 5.3 support libraries used by Arduino core |
| PlatformIO | Installed toolchain manager | Apache-2.0 for PlatformIO Core; package licenses apply | `https://github.com/platformio/platformio-core` | Build, dependency and upload orchestration |

### Direct Firmware Libraries

| Library | Version | License | Source URL | Use |
| --- | --- | --- | --- | --- |
| TFT_eSPI | `2.5.43` | MIT-derived / compatible notices in `license.txt` | `https://github.com/Bodmer/TFT_eSPI` | ST7789 TFT drawing and sprite framebuffer |
| RotaryEncoder | `1.6.0` installed (`^1.5.3` requested) | BSD-style license | `http://www.mathertel.de/Arduino/RotaryEncoderLibrary.aspx` | LilyGO rotary encoder decoding |
| ArduinoJson | `7.4.3` installed (`^7.4.2` requested) | MIT | `https://github.com/bblanchon/ArduinoJson` | JSON parsing and serialization |
| FastLED | `3.10.3` installed (`^3.9.12` requested) | MIT | `https://github.com/FastLED/FastLED` | WS2812 LED-ring control |
| TJpg_Decoder | `1.1.0` | FreeBSD-style for wrapper plus Tiny JPEG Decompressor terms | `https://github.com/Bodmer/TJpg_Decoder` | JPEG album-art decoding/rendering |
| ESP8266Audio | `1.9.9` | GPL-3.0 according to included `LICENSE` | `https://github.com/earlephilhower/ESP8266Audio` | WAV/MP3 playback helpers over I2S |
| MicroTFLite | `1.0.4` | Apache-2.0 | `https://github.com/johnosbb/MicroTFLite` | TensorFlow Lite Micro runtime and microfrontend for wake-word inference |

### Arduino Core Libraries Used From Framework

These are not listed in `lib_deps`; they are supplied by
`framework-arduinoespressif32` and used through Arduino ESP32 APIs.

| Library/API | Version source | License | Source URL | Use |
| --- | --- | --- | --- | --- |
| WiFi | Arduino ESP32 `3.1.1` | LGPL-2.1-or-later package metadata | `https://github.com/espressif/arduino-esp32` | LAN connectivity |
| NetworkClientSecure | Arduino ESP32 `3.1.1` | LGPL-2.1-or-later package metadata | `https://github.com/espressif/arduino-esp32` | TLS clients |
| HTTPClient | Arduino ESP32 `3.1.1` | LGPL-2.1-or-later package metadata | `https://github.com/espressif/arduino-esp32` | HTTP(S) calls |
| WebServer | Arduino ESP32 `3.1.1` | LGPL-2.1-or-later package metadata | `https://github.com/espressif/arduino-esp32` | Local ESP web portal and API |
| BLE | Arduino ESP32 `3.1.1` | LGPL-2.1-or-later package metadata | `https://github.com/espressif/arduino-esp32` | BLE WiFi provisioning / pairing advertisement |
| DNSServer | Arduino ESP32 `3.1.1` | LGPL-2.1-or-later package metadata | `https://github.com/espressif/arduino-esp32` | Captive portal DNS |
| Preferences | Arduino ESP32 `3.1.1` | LGPL-2.1-or-later package metadata | `https://github.com/espressif/arduino-esp32` | NVS settings and pairing storage |
| LittleFS / FS | Arduino ESP32 `3.1.1` | LGPL-2.1-or-later package metadata | `https://github.com/espressif/arduino-esp32` | Voice WAV and local file storage |
| Update | Arduino ESP32 `3.1.1` | LGPL-2.1-or-later package metadata | `https://github.com/espressif/arduino-esp32` | OTA firmware writes |
| ESPmDNS | Arduino ESP32 `3.1.1` | LGPL-2.1-or-later package metadata | `https://github.com/espressif/arduino-esp32` | `_djconnect._tcp` discovery |
| ESP_I2S | Arduino ESP32 `3.1.1` | LGPL-2.1-or-later package metadata | `https://github.com/espressif/arduino-esp32` | Speaker/microphone I2S/PDM paths |
| Wire | Arduino ESP32 `3.1.1` | LGPL-2.1-or-later package metadata | `https://github.com/espressif/arduino-esp32` | I2C battery gauge |

### Model Assets And Vendored Data

| Asset | Version / identity | License | Source URL | Use |
| --- | --- | --- | --- | --- |
| ESPHome micro-wake-word `Okay Nabu` model | Vendored TFLite, SHA256 `0689abe1912a95a3318a0d8cb2e67bad0cbcfe3e24dd6e050c75debddfb6f891` | Apache-2.0 | `https://github.com/esphome/micro-wake-word-models` | Local wake-word model |
| DJConnect display/web assets | Repository-owned | MIT unless otherwise stated | This repository | Boot/logo/web icons and embedded RGB565 assets |

### Tooling Packages Used By PlatformIO

| Tool | Version | License | Source URL | Use |
| --- | --- | --- | --- | --- |
| esptoolpy package | `4.8.5` | GPL-2.0-or-later package metadata | `https://github.com/tasmota/esptool` | Flashing ESP32-S3 images |
| mklittlefs | `3.2.0` | MIT | `https://github.com/jason2866/mklittlefs` | LittleFS image tooling |
| mkfatfs | `2.0.1` | MIT | `https://github.com/labplus-cn/mkfatfs` | FFat image tooling supplied by platform |
| mkspiffs | `2.230.0` | MIT | `https://github.com/igrr/mkspiffs` | SPIFFS image tooling supplied by platform |
| xtensa ESP ELF GDB | `14.2.0+20240403` | GPL-3.0-or-later | `https://github.com/espressif/binutils-gdb.git` | Debug tooling |
| RISC-V ESP ELF GDB | `14.2.0+20240403` | GPL-3.0-or-later | `https://github.com/espressif/binutils-gdb.git` | Debug tooling supplied by platform |
| Xtensa ESP GCC toolchain | `13.2.0+20240530` | GPL-2.0-or-later package metadata plus GCC runtime/library exceptions where applicable | `https://github.com/espressif/crosstool-NG` | Firmware compilation for ESP32-S3 |
| RISC-V ESP GCC toolchain | `13.2.0+20240530` | GPL-2.0-or-later package metadata plus GCC runtime/library exceptions where applicable | `https://github.com/espressif/crosstool-NG` | Platform-supplied compilation tools |

### Ignored / Non-Linked Transitive Libraries Present In `libdeps`

`MicroTFLite` declares Arduino library dependencies that are downloaded by
PlatformIO but ignored by this firmware through `lib_ignore` in `platformio.ini`.

| Library | Version | License | Source URL | Status |
| --- | --- | --- | --- | --- |
| ArduinoBLE | `2.0.2` | LGPL-2.1 | `https://www.arduino.cc/en/Reference/ArduinoBLE` | Ignored by `lib_ignore`; firmware uses Arduino ESP32 BLE instead |
| Arduino_LSM9DS1 | `1.1.1` | LGPL-2.1 | `https://github.com/arduino-libraries/Arduino_LSM9DS1` | Ignored by `lib_ignore`; not used |
| Arduino_SpiNINA | `0.0.2` | MPL-2.0 | `http://www.arduino.cc/en/Reference/Arduino_SpiNINA` | Ignored by `lib_ignore`; not used |

## Release Maintenance Rule

For every future release, update this document when any of the following change:

- module ownership, control flow, API contracts or board support;
- coding conventions or language/tooling usage;
- PlatformIO environments, build flags, libraries, framework versions or
  vendored assets;
- release artifact naming, manifest shape or OTA behavior;
- embedded HTTPS trust material such as the GitHub OTA CA/certificate bundle.

Release hygiene should update this file together with `README.md`,
`CHANGELOG.md`, `HANDOFF.md`, `TODO.md`, `THIRD_PARTY_NOTICES.md`,
`NEW_CHAT_PROMPT.md`, Postman collections and tests when relevant. Cross-repo
sync prompts are maintained only in `pcvantol/djconnect/SYNC_PROMPTS.md`; when
an ESP firmware change updates a shared contract, make a follow-up change in
`pcvantol/djconnect` instead of adding a local prompt copy. Product roadmap
content is maintained only in `pcvantol/djconnect/PRODUCT_ROADMAP.md`; when
firmware work changes roadmap-relevant scope, update the Home Assistant
integration repo separately instead of adding a local roadmap copy.
