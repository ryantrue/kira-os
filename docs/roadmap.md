# Roadmap

## M0 — Repository and contracts

- Reproducible ESP-IDF/Brookesia dependency line
- ESP32-P4 rev1.3 profile
- Board descriptor and Kira state machine
- CI policy and documentation

## M1 — Hardware bring-up

- Generate local board-manager configuration
- Verify ST7703 timing, GT911 probing and PPA/DMA2D path
- Verify ES8311 playback and ES7210 four-slot capture
- Verify ESP32-C6 SDIO networking and SD card

## M2 — Kira surface

- 60 fps audio-reactive sphere with bounded GPU/memory cost
- Tap-to-desktop and idle return
- Offline, mute and error visuals
- Frame-time and heap telemetry

## M3 — Voice assistant

- WakeNet 9 `Hey, Kira` model partition
- Brookesia Agent Manager and OpenAI Realtime adapter
- Runtime credential provisioning and selectable female voice
- Barge-in, timeouts and privacy validation

## M4 — Full product shell

- System Super launcher, Settings, Files and App Store
- Signed BPK application flow
- A/B OTA with rollback and separately versioned assets/models
- Home Assistant as an optional application

