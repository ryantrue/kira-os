/* SPDX-License-Identifier: Apache-2.0 */
#include "kira/platform/updater.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_log.h"
#include "psa/crypto.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "http_util.hpp"
#include "kira/boot.hpp"
#include "kira/platform/sdcard.hpp"
#include "kira/platform/semver.hpp"
#include "kira/platform/version.hpp"
#include "kira/platform/worker.hpp"
#include "kira/recovery_protocol.h"

namespace kira::platform::updater {
namespace {

constexpr const char *TAG = "kira_update";
constexpr const char *TMP_FILE = KIRA_RECOVERY_DIR "/update.tmp";
constexpr size_t MAX_RELEASE_JSON = 256 * 1024;
constexpr size_t MAX_IMAGE_BYTES = 16 * 1024 * 1024;  // ota_0

struct Release {
    std::string version;       // without 'v'
    std::string name;
    std::string asset_url;
    uint32_t asset_size = 0;
    std::string sha256_hex;    // may be empty until fetched
    std::string checksum_url;  // kira_os.bin.sha256, optional
};

std::mutex s_mutex;
Status s_status;
Release s_release;

void publish(const std::function<void(Status &)> &update)
{
    std::lock_guard lock(s_mutex);
    update(s_status);
    ++s_status.generation;
}

void fail(const std::string &message)
{
    ESP_LOGE(TAG, "%s", message.c_str());
    publish([&](Status &s) {
        s.phase = Phase::Failed;
        s.message = message;
    });
}

std::string api_url()
{
    return std::string("https://api.github.com/repos/") + CONFIG_KIRA_UPDATE_GITHUB_REPO + "/releases/latest";
}

bool is_hex_sha256(const std::string &text)
{
    if (text.size() != 64) {
        return false;
    }
    for (char c : text) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

std::string lower(std::string text)
{
    for (char &c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

std::string json_string(const cJSON *object, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : "";
}

bool parse_release(const std::string &body, Release &release, std::string &error)
{
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        error = "Unreadable answer from GitHub";
        return false;
    }
    const std::string tag = json_string(root, "tag_name");
    const auto parsed = parse_semver(tag);
    if (!parsed) {
        cJSON_Delete(root);
        error = "Latest release tag is not a version: " + tag;
        return false;
    }
    release.version = parsed->to_string();
    release.name = json_string(root, "name");
    const std::string asset_name = CONFIG_KIRA_UPDATE_ASSET;
    const std::string checksum_name = asset_name + ".sha256";
    const cJSON *assets = cJSON_GetObjectItemCaseSensitive(root, "assets");
    const cJSON *asset = nullptr;
    cJSON_ArrayForEach(asset, assets) {
        const std::string name = json_string(asset, "name");
        if (name == asset_name) {
            release.asset_url = json_string(asset, "browser_download_url");
            const cJSON *size = cJSON_GetObjectItemCaseSensitive(asset, "size");
            release.asset_size = cJSON_IsNumber(size) ? static_cast<uint32_t>(size->valuedouble) : 0;
            const std::string digest = json_string(asset, "digest");
            if (digest.rfind("sha256:", 0) == 0) {
                release.sha256_hex = lower(digest.substr(7));
            }
        } else if (name == checksum_name) {
            release.checksum_url = json_string(asset, "browser_download_url");
        }
    }
    cJSON_Delete(root);
    if (release.asset_url.empty()) {
        error = "Release " + release.version + " has no " + asset_name;
        return false;
    }
    if (release.asset_size == 0 || release.asset_size > MAX_IMAGE_BYTES) {
        error = "Release image size is invalid";
        return false;
    }
    return true;
}

void check_job()
{
    const std::string current = firmware_semver();
    publish([&](Status &s) {
        s.phase = Phase::Checking;
        s.current = current;
        s.message = "Checking GitHub...";
    });
    detail::HttpRequest request;
    request.url = api_url();
    request.headers.emplace_back("Accept", "application/vnd.github+json");
    const auto response = detail::http_fetch(request, MAX_RELEASE_JSON);
    if (response.err != ESP_OK) {
        if (response.status == 404) {
            fail("No public release found for " CONFIG_KIRA_UPDATE_GITHUB_REPO);
        } else if (response.status != 0) {
            fail("GitHub answered " + std::to_string(response.status));
        } else {
            fail(std::string("No connection to GitHub: ") + esp_err_to_name(response.err));
        }
        return;
    }
    Release release;
    std::string error;
    if (!parse_release(response.body, release, error)) {
        fail(error);
        return;
    }
    const auto running = parse_semver(current);
    const auto latest = parse_semver(release.version);
    const bool newer = running && latest && compare_semver(*latest, *running) > 0;
    {
        std::lock_guard lock(s_mutex);
        s_release = release;
    }
    publish([&](Status &s) {
        s.latest = release.version;
        s.release_name = release.name;
        s.total_bytes = release.asset_size;
        s.phase = newer ? Phase::Available : Phase::UpToDate;
        s.message = newer ? "Version " + release.version + " is available" : "Kira is up to date";
    });
}

bool fetch_checksum(Release &release, std::string &error)
{
    if (is_hex_sha256(release.sha256_hex)) {
        return true;
    }
    if (release.checksum_url.empty()) {
        error = "Release has no checksum; refusing to install";
        return false;
    }
    detail::HttpRequest request;
    request.url = release.checksum_url;
    const auto response = detail::http_fetch(request, 1024);
    if (response.err != ESP_OK) {
        error = "Cannot download the checksum";
        return false;
    }
    release.sha256_hex = lower(response.body.substr(0, 64));
    if (!is_hex_sha256(release.sha256_hex)) {
        error = "Checksum file is malformed";
        return false;
    }
    return true;
}

std::string to_hex(const uint8_t *data, size_t size)
{
    static const char digits[] = "0123456789abcdef";
    std::string text;
    text.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        text.push_back(digits[data[i] >> 4]);
        text.push_back(digits[data[i] & 0x0F]);
    }
    return text;
}

// ESP image header (24 bytes) + first segment header (8 bytes) + esp_app_desc_t.
bool check_image_header(const char *path, const std::string &expected_version, std::string &error)
{
    uint8_t header[32 + 256] = {};
    FILE *file = fopen(path, "rb");
    if (file == nullptr || fread(header, 1, sizeof(header), file) != sizeof(header)) {
        if (file != nullptr) {
            fclose(file);
        }
        error = "Downloaded file is too short";
        return false;
    }
    fclose(file);
    const uint16_t chip_id = static_cast<uint16_t>(header[12] | (header[13] << 8));
    uint32_t desc_magic = 0;
    memcpy(&desc_magic, header + 32, sizeof(desc_magic));
    char version[33] = {};
    char project[33] = {};
    memcpy(version, header + 32 + 16, 32);
    memcpy(project, header + 32 + 48, 32);
    if (header[0] != 0xE9 || chip_id != 18 /* ESP_CHIP_ID_ESP32P4 */) {
        error = "Not an ESP32-P4 firmware image";
        return false;
    }
    if (desc_magic != 0xABCD5432 || strcmp(project, KIRA_RECOVERY_EXPECTED_PROJECT) != 0) {
        error = "Not a Kira OS image";
        return false;
    }
    const auto image_version = parse_semver(version);
    const auto release_version = parse_semver(expected_version);
    if (!image_version || !release_version || compare_semver(*image_version, *release_version) != 0) {
        error = std::string("Image version ") + version + " does not match the release";
        return false;
    }
    return true;
}

void install_job()
{
    Release release;
    {
        std::lock_guard lock(s_mutex);
        release = s_release;
    }
    if (release.asset_url.empty()) {
        fail("Check for updates first");
        return;
    }
    if (!sdcard::is_mounted()) {
        fail("Insert a FAT formatted SD card to install updates");
        return;
    }
    std::string error;
    if (!fetch_checksum(release, error)) {
        fail(error);
        return;
    }
    mkdir(KIRA_RECOVERY_DIR, 0775);
    FILE *file = fopen(TMP_FILE, "wb");
    if (file == nullptr) {
        fail("Cannot write to the SD card");
        return;
    }
    if (psa_crypto_init() != PSA_SUCCESS) {
        fclose(file);
        fail("Crypto init failed");
        return;
    }
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    psa_hash_setup(&hash, PSA_ALG_SHA_256);

    publish([&](Status &s) {
        s.phase = Phase::Downloading;
        s.download_bytes = 0;
        s.total_bytes = release.asset_size;
        s.message = "Downloading " + release.version + "...";
    });
    uint32_t received = 0;
    uint32_t last_report = 0;
    bool write_error = false;
    detail::HttpRequest request;
    request.url = release.asset_url;
    request.timeout_ms = 30000;
    int status = 0;
    const esp_err_t err = detail::http_stream(
        request,
        [&](const char *data, size_t size, size_t) {
            if (received + size > MAX_IMAGE_BYTES || fwrite(data, 1, size, file) != size) {
                write_error = true;
                return false;
            }
            psa_hash_update(&hash, reinterpret_cast<const uint8_t *>(data), size);
            received += size;
            if (received - last_report >= 64 * 1024) {
                last_report = received;
                publish([received](Status &s) { s.download_bytes = received; });
            }
            return true;
        },
        status);
    const bool closed = fclose(file) == 0;
    uint8_t digest[32] = {};
    size_t digest_length = 0;
    psa_hash_finish(&hash, digest, sizeof(digest), &digest_length);

    if (err != ESP_OK || write_error || !closed) {
        unlink(TMP_FILE);
        fail(write_error ? "SD card write failed" : "Download failed");
        return;
    }
    publish([received](Status &s) {
        s.phase = Phase::Verifying;
        s.download_bytes = received;
        s.message = "Verifying...";
    });
    if (received != release.asset_size) {
        unlink(TMP_FILE);
        fail("Downloaded size does not match the release");
        return;
    }
    if (to_hex(digest, digest_length) != release.sha256_hex) {
        unlink(TMP_FILE);
        fail("Checksum mismatch; the download was discarded");
        return;
    }
    if (!check_image_header(TMP_FILE, release.version, error)) {
        unlink(TMP_FILE);
        fail(error);
        return;
    }
    unlink(KIRA_RECOVERY_UPDATE_FILE);
    if (rename(TMP_FILE, KIRA_RECOVERY_UPDATE_FILE) != 0) {
        fail("Cannot prepare the update file");
        return;
    }
    publish([&](Status &s) {
        s.phase = Phase::ReadyToInstall;
        s.message = "Restarting into recovery to install " + release.version;
    });
    ESP_LOGW(TAG, "update %s verified, handing over to recovery", release.version.c_str());
    vTaskDelay(pdMS_TO_TICKS(1500));
    const esp_err_t boot_err = kira::boot::request_install();  // returns only on failure
    fail(std::string("Recovery is not available: ") + esp_err_to_name(boot_err));
}

void run(const char *name, void (*job)())
{
    if (!submit_job(name, job)) {
        fail("Busy, try again");
    }
}

}  // namespace

Status status()
{
    std::lock_guard lock(s_mutex);
    Status copy = s_status;
    if (copy.current.empty()) {
        copy.current = firmware_semver();
    }
    return copy;
}

void check_async()
{
    publish([](Status &s) {
        s.phase = Phase::Checking;
        s.message = "Checking GitHub...";
    });
    run("update_check", check_job);
}

void install_async()
{
    publish([](Status &s) {
        s.phase = Phase::Downloading;
        s.message = "Preparing download...";
    });
    run("update_install", install_job);
}

}  // namespace kira::platform::updater
