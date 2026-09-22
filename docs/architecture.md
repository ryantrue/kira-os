# Architecture

Kira OS is a product composition over ESP-Brookesia System Super rather than a
fork of its shell. Upstream components stay versioned dependencies; product
behavior lives in small local components.

## Layers

1. **Board HAL** — Waveshare display, touch, audio, storage and ESP32-C6 link.
2. **Brookesia services** — display, audio, Wi-Fi, storage, HTTP, SNTP and OTA.
3. **Kira services** — wake policy, assistant lifecycle, privacy state and the
   Kira surface state machine.
4. **System Super shell** — desktop, app launcher, system overlays and BPK apps.
5. **Applications** — settings, files, app store and later Home Assistant.

The shell never owns the microphone policy. `kira_agent` is a system service and
survives app changes. The surface subscribes to state and audio-energy events;
it does not make network or assistant decisions itself.

## Surface states

| State | Meaning | Network audio |
| --- | --- | --- |
| Idle | Ambient reactive sphere; local Kira wake provider active | Off |
| Listening | Wake word accepted; collecting the request | On |
| Thinking | Request committed; waiting for first response | On |
| Speaking | Female assistant voice and speech animation | On |
| Desktop | App launcher is foreground; assistant remains available | Only after wake |
| Offline | Local UI/wake path works without cloud | Off |
| Error | Recoverable diagnostic surface | Off |

## Dependency policy

Release branches pin exact component locks. `main` follows the compatible
`0.8.*` Brookesia line until the first hardware-verified release, after which
updates arrive through reviewed dependency pull requests.
