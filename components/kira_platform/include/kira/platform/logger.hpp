/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace kira::platform::logger {

// Persistent log capture.
//
// Every ESP_LOG line still goes to the UART. When logging is enabled in
// settings, lines are also queued (never blocking the caller) and a writer
// task appends them to:
//   /sdcard/kira/logs    when a FAT card is mounted
//   /littlefs/kira/logs  otherwise (system flash)
// Two files, kira.log and kira.log.1, each up to MAX_FILE_BYTES, so at most
// MAX_TOTAL_BYTES are kept; older lines are dropped.
inline constexpr size_t MAX_FILE_BYTES = 512 * 1024;
inline constexpr size_t MAX_TOTAL_BYTES = 2 * MAX_FILE_BYTES;  // 1 MiB

// Installs the log hook and starts the writer. Call early, after Settings::load().
void start();

// Enable/disable persistent logging (also stored in settings).
void set_enabled(bool enabled);
[[nodiscard]] bool enabled();

struct Status {
    bool enabled = false;
    std::string location;     // directory in use, empty when none is available
    size_t stored_bytes = 0;  // bytes currently kept
    uint32_t dropped_lines = 0;
};
[[nodiscard]] Status status();

// Streams the kept log, oldest first, in chunks. Returns false when a chunk
// could not be delivered. The writer is paused while this runs.
bool read_all(const std::function<bool(const char *data, size_t size)> &sink);

// Deletes the kept log files.
void clear();

// Temporarily stop writing to the SD card (while it is being formatted).
void pause_sd(bool paused);

}  // namespace kira::platform::logger
