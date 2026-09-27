/* SPDX-License-Identifier: Apache-2.0 */
#include "kira/platform/logger.hpp"

#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"

#include "kira/platform/sdcard.hpp"
#include "kira/platform/settings.hpp"

namespace kira::platform::logger {
namespace {

constexpr const char *SD_DIR = "/sdcard/kira/logs";
constexpr const char *FLASH_DIR = "/littlefs/kira/logs";
constexpr size_t RING_BYTES = 64 * 1024;
constexpr size_t MAX_LINE_BYTES = 200;           // stack buffer used inside the log call
constexpr TickType_t FLUSH_PERIOD = pdMS_TO_TICKS(2000);

vprintf_like_t s_previous = nullptr;
RingbufHandle_t s_ring = nullptr;
std::atomic<bool> s_enabled{false};
std::atomic<bool> s_sd_paused{false};
std::atomic<uint32_t> s_dropped{0};
std::recursive_mutex s_file_mutex;  // guards the files and s_dir
std::string s_dir;
// Cached for status(): the UI polls it from the LVGL task, which must not touch the filesystem.
std::mutex s_info_mutex;
std::string s_info_location;
std::atomic<size_t> s_stored{0};

int log_hook(const char *format, va_list args)
{
    int written = 0;
    if (s_previous != nullptr) {
        va_list copy;
        va_copy(copy, args);
        written = s_previous(format, copy);
        va_end(copy);
    }
    if (!s_enabled.load(std::memory_order_relaxed) || s_ring == nullptr || xPortInIsrContext()) {
        return written;
    }
    char line[MAX_LINE_BYTES];
    int length = vsnprintf(line, sizeof(line), format, args);
    if (length <= 0) {
        return written;
    }
    if (static_cast<size_t>(length) >= sizeof(line)) {
        length = sizeof(line) - 1;
        line[length - 1] = '\n';  // keep line structure for truncated lines
    }
    if (xRingbufferSend(s_ring, line, static_cast<size_t>(length), 0) != pdTRUE) {
        s_dropped.fetch_add(1, std::memory_order_relaxed);
    }
    return written;
}

bool make_dirs(const std::string &path)
{
    std::string partial;
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            partial = path.substr(0, i);
            if (mkdir(partial.c_str(), 0775) != 0 && errno != EEXIST) {
                return false;
            }
        }
    }
    return true;
}

bool dir_available(const char *dir, const char *mount)
{
    struct stat st;
    if (stat(mount, &st) != 0) {
        return false;
    }
    return make_dirs(dir);
}

// Picks the SD card when mounted, otherwise system flash. Called with s_file_mutex held.
std::string select_dir()
{
    if (!s_sd_paused.load() && sdcard::is_mounted() && dir_available(SD_DIR, sdcard::MOUNT_POINT)) {
        return SD_DIR;
    }
    if (dir_available(FLASH_DIR, "/littlefs")) {
        return FLASH_DIR;
    }
    return {};
}

std::string current_file(const std::string &dir)
{
    return dir + "/kira.log";
}

std::string previous_file(const std::string &dir)
{
    return dir + "/kira.log.1";
}

size_t file_size(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 ? static_cast<size_t>(st.st_size) : 0;
}

void rotate_if_needed(const std::string &dir)
{
    const std::string current = current_file(dir);
    if (file_size(current) < MAX_FILE_BYTES) {
        return;
    }
    const std::string previous = previous_file(dir);
    unlink(previous.c_str());
    rename(current.c_str(), previous.c_str());
}

// Returns false when no storage is available yet (early boot: LittleFS and the
// SD card are mounted by Brookesia services after the logger starts).
bool append(const char *data, size_t size)
{
    std::lock_guard lock(s_file_mutex);
    const std::string dir = select_dir();
    if (dir.empty()) {
        return false;
    }
    s_dir = dir;
    rotate_if_needed(dir);
    FILE *file = fopen(current_file(dir).c_str(), "ab");
    if (file == nullptr) {
        return false;
    }
    fwrite(data, 1, size, file);
    fclose(file);
    s_stored.store(file_size(current_file(dir)) + file_size(previous_file(dir)));
    {
        std::lock_guard info(s_info_mutex);
        s_info_location = dir;
    }
    return true;
}

// Batches queued lines and writes them every FLUSH_PERIOD to spare the flash.
// While no storage is mounted yet, lines wait in the batch and the ring buffer
// (about 70 KB), so the start of the boot log is kept.
void writer_task(void *)
{
    static char batch[8 * 1024];
    size_t used = 0;
    TickType_t last_flush = xTaskGetTickCount();
    for (;;) {
        const size_t room = sizeof(batch) - used;
        if (room > MAX_LINE_BYTES) {
            size_t size = 0;
            auto *item = static_cast<char *>(
                xRingbufferReceiveUpTo(s_ring, &size, pdMS_TO_TICKS(500), room));
            if (item != nullptr) {
                memcpy(batch + used, item, size);
                used += size;
                vRingbufferReturnItem(s_ring, item);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        const bool full = used >= sizeof(batch) - MAX_LINE_BYTES;
        const bool due = (xTaskGetTickCount() - last_flush) >= FLUSH_PERIOD;
        if (used == 0 || !(full || due)) {
            if (used == 0) {
                last_flush = xTaskGetTickCount();
            }
            continue;
        }
        if (!s_enabled.load() || append(batch, used)) {
            used = 0;
        }
        last_flush = xTaskGetTickCount();
    }
}

}  // namespace

void start()
{
    if (s_ring != nullptr) {
        return;
    }
    s_ring = xRingbufferCreateWithCaps(RING_BYTES, RINGBUF_TYPE_BYTEBUF, MALLOC_CAP_SPIRAM);
    if (s_ring == nullptr) {
        ESP_LOGE("kira_log", "no memory for the log buffer");
        return;
    }
    // Internal-RAM stack: the writer touches flash (LittleFS) and the SD card.
    if (xTaskCreatePinnedToCore(writer_task, "kira_log", 4096, nullptr, 2, nullptr, 1) != pdPASS) {
        ESP_LOGE("kira_log", "cannot start the log writer");
        return;
    }
    s_enabled.store(Settings::instance().log_enabled());
    s_previous = esp_log_set_vprintf(log_hook);
}

void set_enabled(bool enabled)
{
    s_enabled.store(enabled);
    Settings::instance().set_log_enabled(enabled);
}

bool enabled()
{
    return s_enabled.load();
}

Status status()
{
    Status result;
    result.enabled = s_enabled.load();
    result.dropped_lines = s_dropped.load();
    result.stored_bytes = s_stored.load();
    std::lock_guard info(s_info_mutex);
    result.location = s_info_location;
    return result;
}

bool read_all(const std::function<bool(const char *data, size_t size)> &sink)
{
    std::lock_guard lock(s_file_mutex);
    const std::string dir = s_dir.empty() ? select_dir() : s_dir;
    if (dir.empty()) {
        return true;
    }
    static char chunk[4096];
    for (const std::string &path : {previous_file(dir), current_file(dir)}) {
        FILE *file = fopen(path.c_str(), "rb");
        if (file == nullptr) {
            continue;
        }
        size_t n = 0;
        bool ok = true;
        while (ok && (n = fread(chunk, 1, sizeof(chunk), file)) > 0) {
            ok = sink(chunk, n);
        }
        fclose(file);
        if (!ok) {
            return false;
        }
    }
    return true;
}

void clear()
{
    std::lock_guard lock(s_file_mutex);
    for (const char *dir : {SD_DIR, FLASH_DIR}) {
        unlink(current_file(dir).c_str());
        unlink(previous_file(dir).c_str());
    }
    s_dropped.store(0);
    s_stored.store(0);
}

void pause_sd(bool paused)
{
    std::lock_guard lock(s_file_mutex);
    s_sd_paused.store(paused);
    s_dir.clear();
}

}  // namespace kira::platform::logger
