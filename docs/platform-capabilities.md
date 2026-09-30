# Kira 0.3 platform capabilities

The platform worker owns slow filesystem, NVS and network operations. Settings
polls cached results on LVGL. These code changes require the exact candidate's
CI build and physical tests; the older 0.2 hardware results do not verify 0.3.

## GPIO and connectivity

`tools/kira_board_metadata.py` generates reservations from the local Board
Manager YAML, board-owned setup code and the resolved `sdkconfig.h`. The SD
format fallback consumes this generated configuration too, so application code
does not duplicate the six SD pin numbers or their power channel.

The GPIO inspector calls ESP-IDF 6.0.1 `gpio_get_io_config`. Input-enabled pads
show a sampled logic level; output-only pads say that input sensing is disabled.
It never enables input or rewrites pad configuration. Known board and host pins
are System/Reserved. All other pads remain read-only because absence from a
peripheral descriptor does not establish safe, exposed board wiring. No output
control is offered before a board-level free-pin allowlist has been verified.
MIPI DSI's dedicated pins are not ordinary GPIO controls.

Wi-Fi association is read through `esp_wifi_sta_get_ap_info` on the worker and
cached for at least five seconds. Credentials remain in the stock Wi-Fi page.
The product does not initialize a Bluetooth service or BLE host. The available
Brookesia HAL source describes BLE on ESP32-P4 as requiring controller-disabled
NimBLE, the ESP-Hosted VHCI transport and compatible companion firmware. None of
those capabilities is inferred merely from the ESP32-C6 model. Bluetooth is
reported unavailable; the companion is not reflashed and no switch is offered.

## Home Assistant

The optional REST client supports saved HTTP/HTTPS base URL and long-lived
access token, `/api/` connection testing, `/api/states` discovery (up to 64
lights, switches, input booleans and fans), toggle and individual state readback.
An entity must have been discovered and have an `on` or `off` state before it can
be toggled. Requests carrying credentials do not follow redirects. An unsuccessful
readback is reported separately from the accepted toggle rather than reporting
an unverified new state. Repeated taps cannot queue duplicate operations.

Secrets are written to NVS through the worker, never logged or echoed in a form.
An empty API-key/token keyboard result keeps the existing secret. URL and secret
values are bounded; tokens with line breaks are rejected. NVS encryption is not
silently enabled. Existing documented provisioning and storage assumptions
continue to apply.

## SD and updates

Formatting is restricted to the board's `/sdcard` FAT volume and requires the
Settings destructive confirmation. The mounted-card path preserves the existing
mount. The failed-mount path creates FAT using the board settings, unmounts the
temporary instance, then asks Board Manager to mount it. Both paths refresh
capacity/free-space status; errors are returned to the UI. Logger SD writes are
paused during formatting. The shared-controller SD shim remains active. There is
no operation that formats internal LittleFS.

The release updater uses the public GitHub latest-release API without credentials.
It accepts a newer SemVer, validates the advertised image size, requires HTTPS
asset URLs, verifies every SHA-256 API result and the final digest, and checks
ESP32-P4 image/project/version before handing the SD image to recovery. Download
writes are flushed and synced. Failed downloads and verification leave the
running partition untouched. Concurrent checks/install taps are ignored. The
Settings UI must obtain an explicit install confirmation; `install_async` also
requires a successful `Available` state.

GitHub's latest-release endpoint excludes prereleases. The stock 0.2 updater
therefore cannot discover a prerelease candidate, and 0.2 has no supported UI
that stages an arbitrary local image for recovery. The end-to-end 0.2-to-0.3
OTA test remains gated on physical acceptance and publication of the final
public v0.3.0 release. Publishing that stable release merely to expose an
untested candidate is not an acceptable test.

## Validation

Run `python tools/test_board_metadata.py` from the activated IDF environment.
It checks the actual board reservations, host SDIO/reset extraction, and that a
changed SD descriptor changes both the reservation and generated format config.
The existing `kira_platform` host test covers SemVer/provider invariants.

Physical acceptance after green CI:

- Open GPIO settings and compare reserved I2C/audio/backlight/display/SD/host
  entries with the board descriptor. Read-only inspection must not interrupt
  display, touch, audio, Wi-Fi or SD. Output-only pads must not display a fake low.
- For Home Assistant, save credentials, restart, test connection, list a light
  and switch, toggle each and confirm their state in Home Assistant. Test an
  invalid token, offline server, unavailable entity and a failed state readback.
- With a disposable SD card, cancel format first, then confirm and verify FAT
  capacity/free space updates and files can be created. Exercise no-card and
  mount-failure cases; the OS must remain responsive. Check LittleFS is intact.
- Check/update against an appropriate public release; reject an altered digest,
  truncated file and wrong image. Confirm the current app still boots after each
  failed pre-install attempt. Recovery backup/install/stable-confirmation and
  rollback require their separate physical acceptance procedure.
