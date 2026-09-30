# Architecture

Kira OS is a product composition over ESP-Brookesia System Super rather than a
fork of its shell. Upstream components stay versioned dependencies; product
behavior lives in small local components.

## Layers

1. **Board HAL** — Waveshare display, touch, audio, storage and ESP32-C6 link.
2. **Brookesia services** — display, audio, Wi-Fi, storage, HTTP, SNTP and OTA.
3. **Kira platform** — cached settings, hardware status, SD, logs, Home
   Assistant, updater and recovery hand-off on one background worker.
4. **System Super shell** — desktop, app launcher, system overlays and BPK apps.
5. **Applications** — native Kira, the product-pinned stock Settings app, files
   and app store.

The launcher owns Kira through `IApp`/`IAppProvider`. `on_start` creates the
surface and voice worker, `on_pause` closes the voice gate and hides the surface,
and `on_stop` closes the gate and deletes the LVGL timer/root. The surface only
renders state and dispatches tap/settings/close actions.

The tap-to-talk path provisions the pinned OpenAI agent from the RAM settings
cache without sending the key through generic service logging. AgentManager
owns activation and session state. Kira's rev1.3 audio device reads the raw board
codec recorder, selects one channel, encodes mono 16 kHz/60 ms Opus, and decodes
response Opus to the codec player. Physical acceptance must identify the best
ES7210 slot; the descriptor currently exposes FL, RE, FR and FC and the adapter
uses the first slot consistently.

Kira configuration is a dynamic page in the stock Settings app. A small bridge
subscribes to its actions, uses `SystemApi::show_keyboard` and destructive
system dialogs, submits slow work to the platform worker, and updates bindings
from cached status. The 0.2 Kira Center component builds no sources.

## Surface states

| State | Meaning | Network audio |
| --- | --- | --- |
| Idle | Tap-to-talk sphere; recorder closed | Off |
| Listening | Explicitly activated conversation; collecting the request | On |
| Thinking | Request committed; waiting for first response | On |
| Speaking | Female assistant voice and speech animation | On |
| Desktop | App is paused or closed; voice gate closed | Off |
| Offline | Local UI and microphone test remain available | Off |
| Error | Recoverable diagnostic surface | Off |

## Dependency policy

Release branches pin exact component locks. `main` follows the compatible
`0.8.*` Brookesia line until the first hardware-verified release, after which
updates arrive through reviewed dependency pull requests.
