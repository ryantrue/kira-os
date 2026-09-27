# Kira Center

Kira's own settings and tools. Version 0.2.0.

## Where it lives and why

Kira Center is an LVGL screen on the display's top layer, the same layer as the
Kira surface. It opens from:

- the floating **K** button on the right edge of the screen (always available);
- the gear button on the Kira surface.

It is not a page inside Brookesia Settings and not a launcher app. Brookesia's
Settings app is a managed component with JSON screens; adding pages there means
forking it, which AGENTS.md forbids (upstream updates would overwrite it).
Registering a native launcher app is possible through
`system_core::IAppProvider`, but its GUI contract has not been verified on
hardware yet; it is the planned next step and can open this same screen.

The UI text is English: the built-in LVGL fonts (Montserrat) have no Cyrillic.
Russian UI needs a font with Cyrillic glyphs and is a separate step.

## Pages

| Page | What it does |
| --- | --- |
| Kira | Auto-start switch (default **off**), inactivity delay 1/5/15 min, open Kira now, speaker test tone |
| AI | Provider (OpenAI, Anthropic, xAI, Google, Kira Brain), model, API key, Kira Brain address |
| Home | Home Assistant address and token, connection test, lights/switches/fans with on/off switches |
| SD card | Mount state, total/free/used, FAT32 format with confirmation (also for exFAT cards) |
| Logs | Save log on/off, where and how much is stored, share over Wi-Fi, delete |
| Updates | Version and build date, check GitHub, download and install through recovery |

### Assistant auto-start

Controls only the Kira surface. Boot safety (`kira::boot::start()`), recovery,
rollback, System Super, display, storage and audio always start.

- Off (default): the desktop is shown after boot; Kira opens only on request.
- On: Kira is shown after boot and again after the chosen inactivity period
  (`lv_display_get_inactive_time`, i.e. no touches).

While hidden, the Kira surface skips its 60 fps animation.

### AI providers

The provider table is `components/kira_platform/src/ai_providers.cpp`; the UI
is generated from it. Model names are editable defaults. Keys are stored per
provider. The realtime voice session in this build is OpenAI
(`brookesia_agent_openai`) or Kira Brain; the other providers are reached
through Kira Brain (docs/assistant-architecture.md). This page stores the
choice; wiring it into the voice session is the assistant milestone.

### Home Assistant

REST API with a long-lived access token. Lists `light`, `switch`,
`input_boolean` and `fan` entities (up to 64) and toggles them through
`homeassistant.toggle`. HTTPS uses the ESP-IDF certificate bundle; plain HTTP
works for local addresses.

### SD card

The card is mounted by ESP Board Manager (`fs_sdcard`, `/sdcard`). Formatting:

- mounted card: `esp_vfs_fat_sdcard_format` in place;
- card that failed to mount (no FAT, e.g. exFAT): Kira brings the bus up with
  the board's SDMMC settings, lets FATFS create a FAT volume, then asks Board
  Manager to mount it.

FATFS in this build has no exFAT, so the result is FAT32 on cards above 2 GB.
Brookesia's file browser sees a newly mounted card after a restart.

The SD card is storage only. The ESP32-P4 has no demand paging, so a card
cannot serve as swap.

### Logging

With logging on, every log line still goes to the UART and is also queued
without blocking the caller. A writer task appends batches every 2 s to
`/sdcard/kira/logs` when a card is mounted, otherwise to `/littlefs/kira/logs`.
Two files of 512 KB each keep at most 1 MB; older lines are removed. Lines
logged before storage is mounted wait in RAM (about 70 KB), so the start of the
boot log is kept.

Share over Wi-Fi opens a WPA2 access point `Kira-XXXX` with a random
password shown on screen, next to the normal Wi-Fi connection, and serves:

- `http://192.168.4.1/` - page with links
- `http://192.168.4.1/log` - downloads `kira-log-<version>.txt`, then deletes
  the log on the device (only after the whole file was sent)
- `http://192.168.4.1/log?keep=1` - downloads without deleting

The same addresses work on the home network IP while sharing is on. Sharing
stops after 15 minutes or with the Stop button.

### Updates

Source: the latest GitHub Release of `CONFIG_KIRA_UPDATE_GITHUB_REPO`
(default `ryantrue/kira-os`). The repository must be readable without a
token; for a private code repository publish releases to a separate public
one and change the option.

Flow: check the release, compare SemVer with the running version, download
`kira_os.bin` to `/sdcard/kira/update.tmp`, verify size, SHA-256 (GitHub asset
digest or `kira_os.bin.sha256`), ESP32-P4 image header, project name and
version, rename to `update.bin`, reboot into recovery. Recovery backs up the
running build, installs the update, and the new build must confirm itself
within 30 s or the previous one is restored.

Releases are produced by `.github/workflows/release.yml` for tags `vX.Y.Z`
that equal `version.txt`.

**Limitation:** the update replaces the application only. System resources in
LittleFS (Brookesia screens, fonts) are not updated. Releases must not change
those resources until firmware+resources bundles (patch 0002) are merged.

## Audio: why the speaker was silent

`brookesia_service_audio` needs the HAL interface `Audio:Playback`. In
`brookesia_hal_adaptor` 0.8.4 it is published only by the audio processor
implementation, which is disabled on rev1.3 because it depends on ESP-SR
(rev3 only). So the service could not start: no speaker, no volume, no mute,
whatever the I2S pins. Pins were not the cause: ES8311/ES7210 on I2S
GPIO9-13 and PA on GPIO53 match Waveshare's hardware description for this
board family.

`components/kira_audio` publishes `Audio:Playback` on top of the codec player
(ES8311 + PA), which works on rev1.3. It plays PCM WAV and raw PCM; compressed
formats need the processor path. Settings volume and mute now reach the codec.

## Keyboard

The 720x720 screen falls into System Super's `default` constants, sized for
small screens. `tools/kira_resource_overrides.py` patches the staged copy at
build time with proportions from Brookesia's 1024x600 variant (keys 330 dp,
24 sp). Managed components are not modified. Kira Center uses its own LVGL
keyboard (320 px, 28 px font).

## Security notes

- Secrets (AI keys, Home Assistant token) are in plain NVS. NVS encryption
  needs an eFuse key (irreversible) and is a separate decision. They are never
  logged or shown after saving.
- While log sharing is on, anyone on the home network can download the log.
- Updates are verified by checksum, not signature. Signed images need a
  signing key and are planned together with Wi-Fi updates in recovery.

## SD card and Wi-Fi share one SDMMC controller

The ESP32-P4 has one SDMMC controller with two slots: slot 0 for the microSD
card, slot 1 for the ESP32-C6 Wi-Fi coprocessor (ESP-Hosted). In ESP-IDF 6.0
the controller can be claimed only once, and ESP-Hosted claims it before
`app_main`, so the card never mounted:

    SD_HOST: sd_host_create_sdmmc_controller(84): no available sd host controller
    vfs_fat_sdmmc: host init failed (0x105)

Worse, tearing down a failed SD mount deletes the controller Wi-Fi still uses
and can crash the board on the next SDIO interrupt.
`components/kira_platform/src/sd_host_shim.c` wraps `sdmmc_host_init` and the
deinit functions (`-Wl,--wrap`) so both slots share the controller and it is
never torn down. Upstream: espressif/esp-idf#17889. Recovery does not start
ESP-Hosted and does not need the shim, which is why the card worked there.

## Hardware assumptions to verify on the board

1. microSD power from on-chip LDO channel 4 (`board_devices.yaml`); recovery
   already reached the card with it.
2. Speaker: the test tone plays; Settings volume changes loudness.
3. Wi-Fi access point via ESP-Hosted runs together with the station.
4. The floating K button does not cover System Super controls.
