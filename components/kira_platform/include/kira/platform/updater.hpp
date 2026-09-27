/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <cstdint>
#include <string>

namespace kira::platform::updater {

// Firmware updates from GitHub Releases (CONFIG_KIRA_UPDATE_GITHUB_REPO).
//
// Release contract (produced by .github/workflows/release.yml):
//   tag         vMAJOR.MINOR.PATCH, equal to version.txt of that commit
//   asset       kira_os.bin (CONFIG_KIRA_UPDATE_ASSET)
//   checksum    the asset's GitHub "digest" (sha256:...) or kira_os.bin.sha256
//
// Install path: download to /sdcard/kira/update.tmp, verify size, SHA-256,
// ESP32-P4 image header and project name, rename to /sdcard/kira/update.bin and
// hand over to Kira recovery (kira::boot::request_install), which backs up the
// running build and installs the new one with rollback. A FAT SD card is required.
enum class Phase : uint8_t {
    Idle,
    Checking,
    UpToDate,
    Available,
    Downloading,
    Verifying,
    ReadyToInstall,
    Failed,
};

struct Status {
    Phase phase = Phase::Idle;
    std::string current;       // running version (SemVer part)
    std::string latest;        // tag of the latest release, without the leading 'v'
    std::string release_name;
    uint32_t download_bytes = 0;
    uint32_t total_bytes = 0;
    std::string message;
    uint32_t generation = 0;
};

[[nodiscard]] Status status();
void check_async();
// Downloads and verifies the latest release, then reboots into recovery to install it.
void install_async();

}  // namespace kira::platform::updater
