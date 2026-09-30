/* SPDX-License-Identifier: Apache-2.0 */
#include "kira/platform/settings.hpp"

#include <algorithm>
#include <vector>

#include "esp_log.h"
#include "nvs.h"

#include "kira/platform/ai_providers.hpp"
#include "kira/platform/worker.hpp"

namespace kira::platform {
namespace {

constexpr const char *TAG = "kira_settings";
constexpr const char *NAMESPACE = "kira_cfg";

constexpr const char *KEY_AUTOSTART = "autostart";
constexpr const char *KEY_IDLE = "idle_min";
constexpr const char *KEY_AI_PROVIDER = "ai_prov";
constexpr const char *KEY_AI_MODEL = "ai_model";
constexpr const char *KEY_AI_ENDPOINT = "ai_ep";
constexpr const char *KEY_LOG = "log_on";
constexpr const char *KEY_HA_URL = "ha_url";
constexpr const char *KEY_HA_TOKEN = "ha_token";

constexpr size_t MAX_VALUE = 1024;

// NVS keys are limited to 15 characters: "key_" + provider id.
std::string key_name(std::string_view provider_id)
{
    std::string key = "key_" + std::string(provider_id);
    if (key.size() > 15) {
        key.resize(15);
    }
    return key;
}

uint8_t read_u8(nvs_handle_t handle, const char *key, uint8_t fallback)
{
    uint8_t value = fallback;
    if (nvs_get_u8(handle, key, &value) != ESP_OK) {
        return fallback;
    }
    return value;
}

std::string read_str(nvs_handle_t handle, const char *key, std::string_view fallback)
{
    size_t length = 0;
    if (nvs_get_str(handle, key, nullptr, &length) != ESP_OK || length == 0 || length > MAX_VALUE) {
        return std::string(fallback);
    }
    std::vector<char> buffer(length);
    if (nvs_get_str(handle, key, buffer.data(), &length) != ESP_OK) {
        return std::string(fallback);
    }
    return std::string(buffer.data());
}

}  // namespace

Settings &Settings::instance()
{
    static Settings settings;
    return settings;
}

void Settings::load()
{
    std::lock_guard lock(mutex_);
    const auto &fallback = default_ai_provider();
    nvs_handle_t handle;
    const bool opened = nvs_open(NAMESPACE, NVS_READONLY, &handle) == ESP_OK;
    if (opened) {
        autostart_ = read_u8(handle, KEY_AUTOSTART, 0) != 0;
        idle_minutes_ = read_u8(handle, KEY_IDLE, 5);
        log_enabled_ = read_u8(handle, KEY_LOG, 0) != 0;
        ai_provider_ = read_str(handle, KEY_AI_PROVIDER, fallback.id);
        ai_endpoint_ = read_str(handle, KEY_AI_ENDPOINT, "");
        ha_url_ = read_str(handle, KEY_HA_URL, "");
        ha_token_ = read_str(handle, KEY_HA_TOKEN, "");
        for (const auto &provider : ai_providers()) {
            std::string key = read_str(handle, key_name(provider.id).c_str(), "");
            if (!key.empty()) {
                ai_keys_.emplace(std::string(provider.id), std::move(key));
            }
        }
    } else {
        ai_provider_ = std::string(fallback.id);
    }
    if (std::find(IDLE_CHOICES_MIN.begin(), IDLE_CHOICES_MIN.end(), idle_minutes_) == IDLE_CHOICES_MIN.end()) {
        idle_minutes_ = 5;
    }
    const AiProvider *provider = find_ai_provider(ai_provider_);
    if (provider == nullptr) {
        ai_provider_ = std::string(fallback.id);
        provider = &fallback;
    }
    ai_model_ = opened ? read_str(handle, KEY_AI_MODEL, provider->default_model)
                       : std::string(provider->default_model);
    if (opened) {
        nvs_close(handle);
    }
    // Secrets are reported only as present/absent.
    ESP_LOGI(TAG, "loaded: autostart=%d idle=%umin provider=%s key=%s log=%d ha=%s",
             autostart_, idle_minutes_, ai_provider_.c_str(),
             ai_keys_.count(ai_provider_) ? "set" : "unset", log_enabled_,
             ha_url_.empty() ? "unset" : "configured");
}

bool Settings::autostart() const
{
    std::lock_guard lock(mutex_);
    return autostart_;
}

esp_err_t Settings::set_autostart(bool enabled)
{
    std::lock_guard lock(mutex_);
    autostart_ = enabled;
    persist_u8(KEY_AUTOSTART, enabled ? 1 : 0);
    return ESP_OK;
}

uint8_t Settings::idle_minutes() const
{
    std::lock_guard lock(mutex_);
    return idle_minutes_;
}

esp_err_t Settings::set_idle_minutes(uint8_t minutes)
{
    if (std::find(IDLE_CHOICES_MIN.begin(), IDLE_CHOICES_MIN.end(), minutes) == IDLE_CHOICES_MIN.end()) {
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard lock(mutex_);
    idle_minutes_ = minutes;
    persist_u8(KEY_IDLE, minutes);
    return ESP_OK;
}

std::string Settings::ai_provider() const
{
    std::lock_guard lock(mutex_);
    return ai_provider_;
}

esp_err_t Settings::set_ai_provider(std::string_view id)
{
    const AiProvider *provider = find_ai_provider(id);
    if (provider == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard lock(mutex_);
    ai_provider_ = std::string(id);
    ai_model_ = std::string(provider->default_model);
    persist_str(KEY_AI_PROVIDER, ai_provider_);
    persist_str(KEY_AI_MODEL, ai_model_);
    return ESP_OK;
}

std::string Settings::ai_model() const
{
    std::lock_guard lock(mutex_);
    return ai_model_;
}

esp_err_t Settings::set_ai_model(std::string_view model)
{
    if (model.empty() || model.size() >= MAX_VALUE) {
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard lock(mutex_);
    ai_model_ = std::string(model);
    persist_str(KEY_AI_MODEL, ai_model_);
    return ESP_OK;
}

std::string Settings::ai_endpoint() const
{
    std::lock_guard lock(mutex_);
    return ai_endpoint_;
}

esp_err_t Settings::set_ai_endpoint(std::string_view url)
{
    if (url.size() >= MAX_VALUE) {
        return ESP_ERR_INVALID_SIZE;
    }
    std::lock_guard lock(mutex_);
    ai_endpoint_ = std::string(url);
    persist_str(KEY_AI_ENDPOINT, ai_endpoint_);
    return ESP_OK;
}

bool Settings::has_ai_key(std::string_view provider_id) const
{
    return !ai_key(provider_id).empty();
}

std::string Settings::ai_key(std::string_view provider_id) const
{
    std::lock_guard lock(mutex_);
    const auto it = ai_keys_.find(provider_id);
    return it == ai_keys_.end() ? std::string() : it->second;
}

esp_err_t Settings::set_ai_key(std::string_view provider_id, std::string_view key)
{
    if (find_ai_provider(provider_id) == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (key.size() >= MAX_VALUE) {
        return ESP_ERR_INVALID_SIZE;
    }
    // Empty keyboard submissions preserve provisioned credentials.
    if (key.empty()) {
        return ESP_OK;
    }
    if (key.find_first_of("\r\n") != std::string_view::npos) {
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard lock(mutex_);
    ai_keys_[std::string(provider_id)] = std::string(key);
    persist_str(key_name(provider_id), std::string(key));
    return ESP_OK;
}

bool Settings::log_enabled() const
{
    std::lock_guard lock(mutex_);
    return log_enabled_;
}

esp_err_t Settings::set_log_enabled(bool enabled)
{
    std::lock_guard lock(mutex_);
    log_enabled_ = enabled;
    persist_u8(KEY_LOG, enabled ? 1 : 0);
    return ESP_OK;
}

std::string Settings::ha_url() const
{
    std::lock_guard lock(mutex_);
    return ha_url_;
}

esp_err_t Settings::set_ha_url(std::string_view url)
{
    if (url.size() >= MAX_VALUE) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!url.empty()) {
        const size_t prefix = url.starts_with("https://") ? 8 : url.starts_with("http://") ? 7 : 0;
        if (prefix == 0 || url.size() <= prefix || url[prefix] == '/' ||
                url.find_first_of(" \t\r\n@?#") != std::string_view::npos) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    std::lock_guard lock(mutex_);
    ha_url_ = std::string(url);
    while (!ha_url_.empty() && ha_url_.back() == '/') {
        ha_url_.pop_back();
    }
    persist_str(KEY_HA_URL, ha_url_);
    return ESP_OK;
}

bool Settings::has_ha_token() const
{
    return !ha_token().empty();
}

std::string Settings::ha_token() const
{
    std::lock_guard lock(mutex_);
    return ha_token_;
}

esp_err_t Settings::set_ha_token(std::string_view token)
{
    if (token.size() >= MAX_VALUE) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (token.empty()) {
        return ESP_OK;
    }
    if (token.find_first_of("\r\n") != std::string_view::npos) {
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard lock(mutex_);
    ha_token_ = std::string(token);
    persist_str(KEY_HA_TOKEN, ha_token_);
    return ESP_OK;
}

void Settings::persist_u8(const char *key, uint8_t value)
{
    submit_job("settings_save", [key, value] {
        nvs_handle_t handle;
        esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            err = nvs_set_u8(handle, key, value);
            if (err == ESP_OK) {
                err = nvs_commit(handle);
            }
            nvs_close(handle);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "cannot store %s: %s", key, esp_err_to_name(err));
        }
    });
}

// Values are never logged: some keys hold secrets.
void Settings::persist_str(std::string key, std::string value)
{
    submit_job("settings_save", [key = std::move(key), value = std::move(value)] {
        nvs_handle_t handle;
        esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            if (value.empty()) {
                err = nvs_erase_key(handle, key.c_str());
                if (err == ESP_ERR_NVS_NOT_FOUND) {
                    err = ESP_OK;
                }
            } else {
                err = nvs_set_str(handle, key.c_str(), value.c_str());
            }
            if (err == ESP_OK) {
                err = nvs_commit(handle);
            }
            nvs_close(handle);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "cannot store %s: %s", key.c_str(), esp_err_to_name(err));
        }
    });
}

}  // namespace kira::platform
