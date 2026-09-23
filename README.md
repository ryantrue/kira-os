# Kira OS

Kira OS is a touch-first, voice-native operating environment for the
Waveshare ESP32-P4-WIFI6-Touch-LCD-4B. It is built as a product on top of
ESP-IDF 6.0 and ESP-Brookesia System Super 0.8, not as a Home Assistant panel.

The default surface is Kira: an audio-reactive sphere that processes ambient
audio locally and activates the cloud assistant only after the on-device
`Hey, Kira` wake word. A tap opens the application desktop. The assistant stays
available as a system service while applications are running.

## Product contract

- The microphone front end and wake-word detector run locally at all times.
- Audio is not sent to a remote service before wake-word activation.
- Wake-word detection is a replaceable local provider. Revision 1.3 uses a
  Kira-owned TFLite Micro provider because ESP-SR on ESP-IDF 6 requires P4 rev 3.
- The desktop and BPK application model come from ESP-Brookesia System Super.
- Home Assistant is an optional application, never a shell dependency.
- OpenAI credentials are provisioned at runtime and are never compiled into an
  image or committed to the repository.
- Firmware images are hardware-revision specific. The initial target is ESP32-P4
  revision 1.3 with 200 MHz PSRAM.

## Status

The repository is in hardware bring-up. The architecture, state machine, board
descriptor, System Super boot flow, raw audio path, and first on-device Kira
surface are established. The local TFLite Micro wake provider is the next
audio milestone.

## Toolchain

- ESP-IDF `v6.0.1` (CI baseline; see `docs/upstream-updates.md` for the 6.0.2 migration)
- ESP-Brookesia `0.8.x`, resolved versions pinned in `dependencies.lock`
- LVGL `9.x`
- Waveshare BSP `3.0.1`
- Target: `esp32p4`

## Build

```
. "$IDF_PATH/export.sh"
bash scripts/configure.sh
idf.py build
```

The sdkconfig defaults layers (project, Kira performance layer, board, rev1.3
silicon profile) are defined once, in the root `CMakeLists.txt`. Do not pass
defaults through the environment or on the command line.

`scripts/configure.sh` resolves the pinned component graph, generates the board
layer from the local descriptor, lets conditional dependencies converge and then
verifies that the rev1.3 profile survived.

Clean build:

```
idf.py fullclean
rm -rf managed_components sdkconfig
bash scripts/configure.sh
idf.py build
```

## Updating upstream components

`dependencies.lock` is committed; builds use exactly those versions. To move to
newer ESP-Brookesia / Board Manager releases, run `scripts/update-deps.sh`, test
on hardware and commit the new lock. A weekly `upstream-canary` workflow warns
in advance when newer upstream versions would break the build.

See [architecture](docs/architecture.md), [hardware](docs/hardware.md),
[privacy](docs/privacy.md), [performance](docs/performance.md),
[upstream updates](docs/upstream-updates.md) and the [roadmap](docs/roadmap.md).

## License

Apache-2.0. Third-party components retain their own licenses.
