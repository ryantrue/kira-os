# Kira OS 0.4 native experience architecture

0.4 is a UX/performance release. New platform features are intentionally secondary to native placement, responsiveness and predictable lifecycle.

## Ownership

| Surface | Owns | Must not own |
| --- | --- | --- |
| Settings > Sound | Speaker volume/mute, microphone mute/gain/local test | AI provider, HA entities |
| Settings > Connectivity | Wi-Fi and, only when implemented, Bluetooth | Fake Bluetooth controls |
| Settings > Storage | SD presence/capacity/free space/format | AI or HA |
| Settings > Home Assistant | Server address, token, connection test | Entity dashboard/control |
| Settings > Kira | Assistant provider/model/key, assistant behavior, version/update | SD, GPIO, HA entities, board diagnostics |
| Home Assistant app | Entity dashboard and control | Credentials editor |
| Pins / I/O app | Physical expansion-header map, safe user outputs | Arbitrary ESP32 GPIO manipulation |

## Performance gates

Before visual simplification, measure on the physical Waveshare ESP32-P4-WIFI6-Touch-LCD-4B:

1. app cold-start, pause/resume and close latency;
2. LVGL frame/update time while scrolling Settings and opening Kira;
3. internal heap + PSRAM before/after app transitions;
4. CPU/task load during idle UI and interaction;
5. synchronous filesystem/network work on the GUI task.

The display remains 720x720 RGB565 with the existing DMA2D path. Do not lower resolution or remove animation until profiling identifies rendering as the bottleneck.

### Polling rule

A settings page may poll only state visible on that page. Expensive network, filesystem and hardware inventory operations must be event-driven or run on a worker. The 0.3 Kira bridge's one-second aggregate poll is explicitly a migration target.

## Pins / I/O safety

The user-facing Pins app models the **physical expansion connector**, not the ESP32-P4 GPIO namespace.

A pin becomes writable only after its physical connector position and electrical role are verified against the exact 4B board schematic. Power, ground, boot/strapping, display, touch, SD, audio, ESP-Hosted and other system-owned signals are never exposed as writable. Unknown pins remain informational/read-only.

Each connector position must have RU and EN documentation. ON/OFF is sufficient for 0.4; PWM/protocol configuration is deferred.

## Recent apps

First determine whether the pinned Brookesia System Super already exposes a task switcher/recents implementation or gesture hook. Reuse upstream behavior if available. Otherwise implement a lightweight recents surface on System Core lifecycle state: swipe-up gesture, recent app cards, tap to resume/start, swipe card to close.

This is lifecycle-based task switching, not a promise that every app renders concurrently.

## Deferred beyond 0.4

Wake word/local AFE, additional AI provider adapters, generic GPIO tooling, and feature expansion unrelated to native UX/performance.
