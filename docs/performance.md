# Performance

Kira targets smooth UI and low-latency audio on ESP32-P4 rev1.3
(dual-core RISC-V up to 360 MHz, 32 MiB PSRAM at 200 MHz, 720x720 MIPI DSI).

Every change below must be verified on the physical board and reported with
its verification level (see docs/hardware.md). Measure before and after.

## Applied (sdkconfig.defaults.kira)

| Setting | Effect |
| --- | --- |
| `CONFIG_COMPILER_OPTIMIZATION_PERF` | `-O2` for all code |
| `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_360` | maximum clock for pre-v3 silicon |
| `CONFIG_LV_BUILD_EXAMPLES` off | ~260 fewer sources per build |

## Candidates (commented out in sdkconfig.defaults.kira)

- `CONFIG_SPIRAM_XIP_FROM_PSRAM`: code runs from PSRAM, so flash writes
  (LittleFS, NVS, OTA) no longer disable the cache and stall UI/audio tasks.
  Costs PSRAM roughly equal to the image size.
- `CONFIG_FREERTOS_HZ=1000`: finer scheduling for LVGL and audio tasks.

## Rendering

- Keep LVGL drawing on the P4 hardware paths: PPA (scale/rotate/blend/fill) and
  2D-DMA. The Brookesia LVGL adapter already contains a PPA bridge; confirm it
  is active at runtime before adding LVGL's own `LV_USE_PPA`, never both.
- Full-screen double buffering in PSRAM with DSI tear avoidance; one
  720x720 RGB565 frame is ~1 MiB.
- The Kira sphere is the always-on surface: prefer pre-rendered layers blended
  by PPA over per-pixel CPU drawing, and cap its refresh rate when idle.

## Tasks and cores

- Pin the LVGL/render task and the audio front end (capture, wake word) to
  different cores; the wake-word path must never wait on UI rendering.
- Keep ISR-adjacent audio buffers in internal RAM; large UI assets in PSRAM.

## Build footprint

Each unused managed component costs build time, flash and often RAM at boot.
Review `main/idf_component.yml` against what `main/` actually uses; candidates
to drop if unused: `brookesia_emulation_nes`, `brookesia_runtime_js`,
`brookesia_service_video`, `brookesia_expression_emote`, `brookesia_app_store`.
`idf.py size-components` shows the per-component cost.

## Wake word

The Kira-owned TFLite Micro provider should use esp-nn optimized kernels.
microWakeWord (the TFLM-based engine used by ESPHome) provides a training
pipeline for custom phrases such as "Hey Kira" and models small enough to run
continuously alongside the UI.
