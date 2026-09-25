# Board bring-up

Target: Waveshare ESP32-P4-WIFI6-Touch-LCD-4B, ESP32-P4 rev1.3.

## First flash

```sh
. "$IDF_PATH/export.sh"
bash scripts/configure.sh
idf.py build
(cd recovery && idf.py build)
scripts/flash.sh PORT --erase
idf.py -p PORT monitor
```

With blank otadata recovery starts first, selects a valid Kira image in ota_0
and reboots. Kira confirms itself after about 30 seconds.

Check chip revision v1.3, the recovery/ota_0 partition layout, 32 MiB PSRAM at
200 MHz, ST7703, GT911, ES8311/ES7210, ESP-Hosted SDIO/C6 version and Wi-Fi.
Recovery mounts microSD as SDMMC slot 0, 4-bit. The recovery implementation
uses on-chip LDO channel 4 for card power; the current Board Manager sdmmc
descriptor specifies slot 0 and 4-bit width but does not expose the power LDO,
so LDO4 remains a board-level recovery constant pending hardware bring-up.

Core dumps use the coredump partition. Diagnostic local settings can be enabled
by copying `sdkconfig.defaults.local.example` to `sdkconfig.defaults.local`;
never commit that file.
