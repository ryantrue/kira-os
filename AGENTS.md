# AGENTS.md — Kira OS engineering context

This file is the operational contract for AI coding agents and human contributors working on Kira OS.
Read it before changing build configuration, board support, dependencies, or hardware-facing code.

## Project

Kira OS is a touch-first, voice-native product for the Waveshare
ESP32-P4-WIFI6-Touch-LCD-4B. It composes ESP-Brookesia System Super rather
than forking it.

Current hardware target:
- SoC: ESP32-P4 **revision 1.3**
- Board: Waveshare ESP32-P4-WIFI6-Touch-LCD-4B
- Flash: 32 MiB
- PSRAM: 200 MHz profile
- Connectivity companion: ESP32-C6 over SDIO
- Display: 720x720 ST7703 over 2-lane MIPI DSI
- Touch: GT911
- Audio playback: ES8311
- Audio capture: ES7210, 4 channels

## Supported software line

Keep these constraints aligned unless a migration is intentional and tested:
- ESP-IDF: >=6.0.2,<6.1; development/CI baseline is v6.0.2
- ESP-Brookesia components: 0.8.x
- Waveshare BSP: 3.0.1
- Target: esp32p4

Do not casually upgrade ESP-IDF, Brookesia, the Waveshare BSP, ESP-Hosted, or
board-manager components independently. They form one compatibility graph.

## Critical ESP32-P4 rev1.3 constraint

This repository targets pre-v3 ESP32-P4 silicon. The following settings are
intentional and must not be removed:

    CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y
    CONFIG_ESP32P4_REV_MIN_100=y
    CONFIG_SPIRAM_SPEED_200M=y
    CONFIG_SPIRAM_SPEED=200

ESP32-P4 rev3.x is a different compatibility profile. Do not flash a Kira
rev1.3 artifact to rev3.x hardware and do not "fix" a dependency conflict by
silently moving the project to a rev3.x-only configuration.

ESP-SR's current prebuilt ESP32-P4 path used by this dependency line is not
compatible with the Kira rev1.3 target. Therefore Brookesia's audio processor
implementation is deliberately disabled. Raw codec capture remains enabled and
Kira owns the local wake-word provider. Do not re-enable the processor merely
to satisfy a component dependency.

## Board descriptor rules

The local board descriptor under:

    boards/waveshare/esp32_p4_wifi6_touch_lcd_4b/

is the source of truth for Kira-specific board integration. Hardware pin
numbers must not be duplicated in application code.

Important: some board-manager device descriptors use 8-bit shifted I2C address
notation. This explains values such as ES8311 0x30 (7-bit 0x18), ES7210 0x80
(7-bit 0x40), and GT911 0xBA/0x28 (7-bit 0x5D/0x14). Do not normalize these
values without checking the board-manager schema and generated code.

Key pins are documented in docs/hardware.md.

## Dependency/bootstrap behavior

ESP Board Manager contributes an idf.py extension only after its component has
been resolved. The root idf_ext.py intentionally provides a bootstrap
gen-bmgr-config action before that point.

scripts/configure.sh performs this sequence:
1. set target to esp32p4;
2. resolve managed components;
3. generate board-manager configuration from the local boards directory;
4. resolve again because generated board configuration can activate additional
   conditional dependencies (ESP-Hosted/media/etc.).

The repeated reconfigure calls are bounded convergence, not arbitrary retries.
If convergence starts requiring more attempts, inspect the component graph and
lockfile rather than increasing the retry count.

## Canonical clean build

Use an ESP-IDF v6.0.2 environment:

    . "$IDF_PATH/export.sh"
    idf.py fullclean
    rm -rf managed_components dependencies.lock
    bash scripts/configure.sh
    idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.rev1_3" build

For an incremental build, do not delete managed_components/dependencies.lock:

    . "$IDF_PATH/export.sh"
    bash scripts/configure.sh
    idf.py build

When changing manifests, board descriptors, sdkconfig defaults, or CMake,
perform at least one clean build before declaring the change fixed.

## Known build failures

### LVGL fsdrv: "Invalid drive letter"

LVGL's POSIX and STDIO filesystem drivers can be enabled by upstream/default
configuration with an invalid default drive letter. Kira does not use those
host-style drivers; it uses MEMFS plus Brookesia storage. Keep
`CONFIG_LV_USE_FS_POSIX=n` and `CONFIG_LV_USE_FS_STDIO=n` unless a real
filesystem integration is added with an explicit valid drive letter.

## Build debugging order

When the firmware build fails:
1. Record ESP-IDF version and the first real CMake/component-manager error.
2. Check dependencies.lock and managed component versions.
3. Check whether failure happened before or after gen-bmgr-config.
4. Inspect generated board configuration before modifying source code.
5. Verify rev1.3 Kconfig survived configuration.
6. Only then patch code or dependency constraints.

Never use --force to bypass chip-revision checks as a normal development fix.
Never weaken TLS/security settings to make dependency downloads work.
Never commit credentials, tokens, Wi-Fi secrets, generated build/, or local
sdkconfig files.

## Validation before commit

At minimum:
- source-contract checks from .github/workflows/quality.yml;
- kira_core state-machine host test;
- firmware configure + build for rev1.3 when build-related files changed.

If hardware-facing behavior changed, report separately whether it was:
- compile-tested only;
- flashed;
- boot-tested;
- peripheral-tested on the physical board.

Do not claim physical verification unless it actually happened.

## Architecture boundaries

- components/kira_core: product state and hardware-independent logic.
- components/kira_surface: Kira LVGL surface.
- main/: Brookesia composition and device/service wiring.
- boards/: board/HAL description and board-specific initialization.
- docs/: architecture, hardware, privacy, roadmap and troubleshooting.

Prefer upstream APIs and board-manager mechanisms over local hard-coded
workarounds. Keep Kira-owned code small and replaceable.

## Product invariants

- Wake-word processing is local.
- Audio must not be streamed remotely before activation.
- Cloud credentials are runtime-provisioned, never compiled in.
- Home Assistant is an optional application, not a shell dependency.
- System Super remains an upstream dependency, not copied/forked into Kira.

## When leaving work for another agent

Document:
- exact failing command;
- first relevant error (not only the final Ninja failure);
- ESP-IDF/component versions;
- commit SHA;
- whether the failure reproduces from a clean build;
- hardware verification status.

Put durable hardware/build knowledge in docs/, and update this file when a new
constraint changes how agents must work.
