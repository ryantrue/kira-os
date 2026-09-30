# Kira OS

Kira OS is a touch-first, voice-native operating environment for the
Waveshare ESP32-P4-WIFI6-Touch-LCD-4B. It is built as a product on top of
ESP-IDF 6.0 and ESP-Brookesia System Super 0.8, not as a Home Assistant panel.

Kira is a native System Super launcher application. Its audio-reactive sphere
starts a bounded voice conversation when tapped, opens the stock Settings app
from the gear button, and releases its LVGL objects, timer and audio session when
closed or paused.

## Product contract

- The microphone recorder is closed while Kira is idle. A tap explicitly opens
  a voice session; pause, close, mute and timeout close its upload gate.
- Kira never sends audio before explicit activation. A future wake-word provider
  must run locally; 0.3.0 does not claim one on revision 1.3.
- The desktop and BPK application model come from ESP-Brookesia System Super.
- Home Assistant is an optional application, never a shell dependency.
- OpenAI credentials are provisioned at runtime and are never compiled into an
  image or committed to the repository.
- Firmware images are hardware-revision specific. The initial target is ESP32-P4
  revision 1.3 with 200 MHz PSRAM.

## Status

Version 0.3.0 replaces the 0.2 Kira Center overlay with one native Kira app and
Kira pages inside `brookesia.general.settings`. The product-owned raw audio
adapter connects the board's `Audio:CodecRecorder` to the Brookesia OpenAI agent
as mono Opus and decodes response Opus to `Audio:CodecPlayer`. Settings exposes
only implemented capabilities: local microphone test/gain/software gate,
speaker test, Home Assistant REST controls, SD status/format, diagnostics,
recovery-backed updates, connectivity status and a read-only board-derived GPIO
inspector.

The last physical baseline remains 0.2.x. A green 0.3.0 CI build is a candidate
for board acceptance, not evidence of runtime hardware verification. See the
[installation candidate guide](docs/installation-candidate.md),
[platform capabilities](docs/platform-capabilities.md), and the archived
[0.2 Kira Center design](docs/kira-center.md).

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
