# Recovery and updates

Kira keeps one application slot (`ota_0`, 16 MiB) instead of two, and puts a
small recovery app in the `factory` slot (2 MiB). The backup of the previous
Kira build lives on the SD card, not in flash.

## Boot chain

1. ROM bootloader: USB download mode is always available.
2. Second-stage bootloader starts `ota_0` when valid; otherwise `factory`.
3. Kira (`ota_0`) or Kira recovery (`factory`, `recovery/`).

## Flash layout

`partitions_32m.system.csv` is flashed. `partitions_32m.csv` is an identical
application view where recovery is a data placeholder, so Kira is size-checked
against ota_0. `scripts/check-partitions.py` enforces synchronization.

| Partition | Size | Purpose |
| --- | --- | --- |
| nvs | 512K | settings and boot protocol |
| otadata | 8K | boot selection/rollback |
| model | 1M | wake-word model |
| recovery | 2M | factory recovery |
| ota_0 | 16M | Kira |
| littlefs_data | 10M | apps/resources/fonts |
| storage | 1M | FAT |
| coredump | 512K | crash dumps |

Updates use `/sdcard/kira/update.bin`; the confirmed previous image is backed
up to `/sdcard/kira/backup.bin`. Recovery refuses to overwrite Kira if backup
creation fails. `kira::boot::start()` confirms a new image after 30 seconds
of stable operation. Three consecutive panic/watchdog resets hand control to
recovery. Recovery itself is updated only over USB.

First system flash: `scripts/flash.sh PORT --erase`. Subsequent complete
system flashes: `scripts/flash.sh PORT`.
