/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <cstdint>
#include <string>

namespace kira::platform {

// microSD card as mounted by ESP Board Manager (device "fs_sdcard", /sdcard).
namespace sdcard {

inline constexpr const char *MOUNT_POINT = "/sdcard";

struct Status {
    bool mounted = false;      // FAT volume available at MOUNT_POINT
    bool busy = false;         // a refresh or format job is running
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    std::string filesystem;    // "FAT" when mounted (FATFS in this build has no exFAT)
    std::string message;       // last result, for the UI
    uint32_t generation = 0;   // increments on every change
};

[[nodiscard]] bool is_mounted();
[[nodiscard]] Status status();

// Background jobs (kira::platform worker). Results appear in status().
void refresh_async();
// Formats the card as FAT (FAT32 on cards > 2 GB) and mounts it at MOUNT_POINT.
// Works for a mounted card and for an unformatted/exFAT card that failed to mount.
// All data on the card is lost. The caller must have asked the user to confirm.
void format_async();

}  // namespace sdcard
}  // namespace kira::platform
