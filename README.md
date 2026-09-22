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
- The default wake model is WakeNet 9 `Hey, Kira`.
- The desktop and BPK application model come from ESP-Brookesia System Super.
- Home Assistant is an optional application, never a shell dependency.
- OpenAI credentials are provisioned at runtime and are never compiled into an
  image or committed to the repository.
- Firmware images are hardware-revision specific. The initial target is ESP32-P4
  revision 1.3 with 200 MHz PSRAM.

## Status

The repository is in hardware bring-up. The architecture, state machine, board
descriptor and reproducible dependency line are established. Display/audio HAL
integration and the first on-device Kira surface are the current milestone.

## Toolchain

- ESP-IDF `v6.0.2` (latest stable line validated by upstream Brookesia CI)
- ESP-Brookesia `0.8.x`
- LVGL `9.x`
- Waveshare BSP `3.0.1`
- Target: `esp32p4`

## Build outline

```bash
. "$IDF_PATH/export.sh"
bash scripts/configure.sh
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.rev1_3" build
```

The configuration script resolves the pinned component line, generates HAL
glue from the local board descriptor, and lets hardware-specific conditional
dependencies converge before compilation.

See [architecture](docs/architecture.md), [hardware](docs/hardware.md),
[privacy](docs/privacy.md), and the [roadmap](docs/roadmap.md).

## License

Apache-2.0. Third-party components retain their own licenses.
