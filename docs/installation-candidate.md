# Kira OS 0.3.0 installation candidate

This guide applies only to the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B with
ESP32-P4 revision 1.3/pre-v3, 32 MiB flash and 32 MiB PSRAM. The CI artifact is
compile- and contract-tested. Its manifest deliberately records
`"hardware_verified": false` until the exact commit is tested on the board.

## Artifact contents and integrity

The `quality` workflow uploads `kira-os-esp32p4-rev1-3`. It contains:

- `bootloader.bin` at `0x00002000`
- `partition-table.bin` at `0x00010000`
- `ota_data_initial.bin` at `0x00091000`
- `kira_recovery.bin` at `0x001A0000`
- `kira_os.bin` at `0x003A0000`
- `littlefs_data.bin` at `0x013A0000`
- `manifest.json`, `SHA256SUMS`, `kira_os.bin.sha256`, `flash_args`,
  `dependencies.lock` and this `INSTALL.md`

Before flashing, run `shasum -a 256 -c SHA256SUMS` in the extracted artifact.
The packager also checks image chip/revision ranges, project/version metadata,
slot sizes, rev1.3 sdkconfig, 200 MHz PSRAM, rollback configuration and the
binary recovery/ota_0/LittleFS partition entries.

Install `esptool` in a Python environment, put the board in USB download mode,
replace `/dev/cu.usbmodemXXXX`, and run from the extracted artifact directory:

```sh
python3 -m esptool --chip esp32p4 --port /dev/cu.usbmodemXXXX --baud 460800 write-flash --flash-mode dio --flash-freq 80m --flash-size 32MB 0x00002000 bootloader.bin 0x00010000 partition-table.bin 0x00091000 ota_data_initial.bin 0x001A0000 kira_recovery.bin 0x003A0000 kira_os.bin 0x013A0000 littlefs_data.bin
```

Use the flash mode/frequency printed in the artifact's `flash_args` if it differs
from the example. Do not use `--force` to bypass the chip revision check. A clean
first install may erase flash first; doing so deletes saved settings and storage.

## Physical acceptance

Keep serial logs for the exact artifact and record each result separately.

First boot:

1. Confirm the ROM reports ESP32-P4 rev1.3 and the bootloader accepts the image.
2. Confirm recovery selects `ota_0`, Kira OS reaches System Super, and
   `kira_boot` reports stable only after System Super start plus 30 seconds.
3. Verify the ST7703 image, GT911 touch, inverted GPIO26 backlight, Wi-Fi and
   32 MiB PSRAM at 200 MHz.
4. Reboot twice and verify normal `ota_0` boot with no recovery loop.

Kira lifecycle:

1. Open Kira from the launcher, close it with the top-left button, then reopen
   it at least ten times.
2. Verify touch latency and animation remain stable and logs show no accumulating
   LVGL timers, roots, tasks or memory loss.
3. Open Settings from Kira, return, pause/resume Kira and close it while a voice
   connection is starting. Confirm the microphone gate closes immediately.

Microphone and voice:

1. In Settings, verify recorder availability, 16 kHz and the reported channel
   count. Run the three-second local record/replay with the network disconnected.
2. Check input level, change gain down/up, repeat the test, then close the
   software capture gate and confirm the test and conversation cannot capture.
3. Play the speaker tone and verify stock volume/mute still affect ES8311/PA53.
4. Save a valid OpenAI key and Realtime model through the system keyboard. Tap
   the Kira sphere, speak, hear a response, end by tapping again, and confirm
   logs contain lifecycle stages but no key, transcript or provider payload.
5. Repeat for network loss, invalid key/model, app close during handshake and the
   two-minute timeout. Inspect all four ES7210 slots if channel zero is silent;
   promote a different board-derived slot only after recording the result.

Settings and capabilities:

1. Confirm there is one stock Settings app and one system keyboard. Empty AI-key
   or HA-token confirmation must preserve the saved secret.
2. Check Assistant, Voice/AI, Microphone/speaker, Home Assistant, Storage/SD,
   Logs/diagnostics, Version/update, Connectivity and GPIO/hardware sections.
3. Wi-Fi remains in the stock page. Bluetooth must say unavailable and expose no
   switch. GPIO rows remain read-only and mark descriptor/system pins reserved.

SD and Home Assistant:

1. With a disposable card, verify present/filesystem/capacity/free/used. Cancel
   format once, then confirm it, wait for remount and create/read a file.
2. Repeat with no card and a mount error; the OS must stay responsive. Reboot and
   verify internal LittleFS applications/resources are intact.
3. Save Home Assistant URL/token, reboot, test `/api/`, refresh entities and
   toggle one light and one switch. Verify state readback in both Kira and Home
   Assistant. Repeat with invalid token, unreachable server and unavailable
   entity; credentialed requests must not follow redirects.

## OTA acceptance from 0.2.x

The 0.2 updater reads GitHub's `/releases/latest`; GitHub excludes prereleases
from that endpoint, and 0.2 has no supported arbitrary staging UI. Therefore the
real 0.2-to-0.3 update is intentionally performed only after this exact candidate
passes the physical checks above.

1. Keep the accepted commit unchanged. Tag it `v0.3.0` only after board sign-off;
   the tag must match `version.txt`. Let the release workflow build and publish
   `kira_os.bin`, its SHA-256 file, recovery binary and complete flash set.
2. Flash the physically verified 0.2.x baseline
   `7f0c720d65645ff4a1f29296d66d971ed4bdef23`, boot it to stable confirmation,
   connect Wi-Fi and insert a FAT SD card with at least 32 MiB free.
3. In 0.2 Kira Center select Check update. Confirm it discovers the public stable
   `v0.3.0`, then approve installation. Do not remove power or the SD card.
4. Verify download size and SHA-256, `/sdcard/kira/backup.bin`, recovery boot,
   write to `ota_0`, pending-verification boot and stable confirmation only after
   System Super has run for 30 seconds.
5. Run the first-boot, lifecycle, voice, Settings, SD and Home Assistant checks
   again. Confirm saved Wi-Fi, AI and HA settings have the expected migration
   behavior and that the old image backup remains restorable.
6. In a controlled second pass, interrupt the new-app boot before confirmation
   or use the documented crash-loop path. Verify recovery restores the SD backup
   and the confirmed 0.2 image remains bootable.

Do not publish the stable tag merely to make an untested candidate visible to
the 0.2 updater.
