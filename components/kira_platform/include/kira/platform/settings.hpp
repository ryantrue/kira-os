/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

#include "esp_err.h"

namespace kira::platform {

// Persistent Kira settings (NVS namespace "kira_cfg").
//
// All values are read once by load() and served from RAM. Setters update RAM
// immediately and persist through the background worker, so UI callbacks never
// touch flash from the LVGL task.
//
// Secrets (AI API keys, Home Assistant token) are stored in NVS and are never
// written to the log; the getters exist only for the code that sends them to
// the corresponding service. NVS encryption needs an eFuse key and is a
// separate, irreversible decision (docs/kira-center.md).
class Settings final {
public:
    static constexpr std::array<uint8_t, 3> IDLE_CHOICES_MIN{1, 5, 15};

    static Settings &instance();

    // Reads all values. Call once after nvs_flash_init() (kira::boot::start()).
    void load();

    // Assistant lifecycle (the Kira surface), not boot safety.
    [[nodiscard]] bool autostart() const;
    esp_err_t set_autostart(bool enabled);
    [[nodiscard]] uint8_t idle_minutes() const;
    esp_err_t set_idle_minutes(uint8_t minutes);  // one of IDLE_CHOICES_MIN

    // AI backend selection; see ai_providers.hpp.
    [[nodiscard]] std::string ai_provider() const;
    esp_err_t set_ai_provider(std::string_view id);
    [[nodiscard]] std::string ai_model() const;
    esp_err_t set_ai_model(std::string_view model);
    [[nodiscard]] std::string ai_endpoint() const;
    esp_err_t set_ai_endpoint(std::string_view url);
    [[nodiscard]] bool has_ai_key(std::string_view provider_id) const;
    [[nodiscard]] std::string ai_key(std::string_view provider_id) const;  // secret
    esp_err_t set_ai_key(std::string_view provider_id, std::string_view key);

    // Persistent logging.
    [[nodiscard]] bool log_enabled() const;
    esp_err_t set_log_enabled(bool enabled);

    // Home Assistant.
    [[nodiscard]] std::string ha_url() const;
    esp_err_t set_ha_url(std::string_view url);
    [[nodiscard]] bool has_ha_token() const;
    [[nodiscard]] std::string ha_token() const;  // secret
    esp_err_t set_ha_token(std::string_view token);

private:
    Settings() = default;
    static void persist_u8(const char *key, uint8_t value);
    static void persist_str(std::string key, std::string value);

    mutable std::mutex mutex_;
    bool autostart_ = false;
    uint8_t idle_minutes_ = 5;
    std::string ai_provider_;
    std::string ai_model_;
    std::string ai_endpoint_;
    bool log_enabled_ = false;
    std::string ha_url_;
    std::string ha_token_;
    std::map<std::string, std::string, std::less<>> ai_keys_;
};

}  // namespace kira::platform
