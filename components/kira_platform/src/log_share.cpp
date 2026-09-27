/* SPDX-License-Identifier: Apache-2.0 */
#include "kira/platform/log_share.hpp"

#include <cstdio>
#include <cstring>
#include <mutex>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "kira/platform/logger.hpp"
#include "kira/platform/version.hpp"

namespace kira::platform::log_share {
namespace {

constexpr const char *TAG = "kira_logshare";

std::mutex s_mutex;
Status s_status;
httpd_handle_t s_server = nullptr;
wifi_mode_t s_previous_mode = WIFI_MODE_NULL;
bool s_mode_changed = false;
esp_timer_handle_t s_stop_timer = nullptr;

std::string ip_url(esp_netif_t *netif)
{
    esp_netif_ip_info_t info = {};
    if (netif == nullptr || esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0) {
        return {};
    }
    char text[48];
    snprintf(text, sizeof(text), "http://" IPSTR "/log", IP2STR(&info.ip));
    return text;
}

esp_err_t index_handler(httpd_req_t *req)
{
    const std::string version = firmware_version();
    const auto log = logger::status();
    char page[768];
    snprintf(page, sizeof(page),
             "<!doctype html><html><head><meta name=viewport content='width=device-width'>"
             "<title>Kira log</title></head><body style='font-family:sans-serif;max-width:32em;margin:2em auto'>"
             "<h2>Kira OS %s</h2><p>Stored log: %u bytes (%s)</p>"
             "<p><a href='/log'>Download and delete from device</a></p>"
             "<p><a href='/log?keep=1'>Download and keep</a></p></body></html>",
             version.c_str(), static_cast<unsigned>(log.stored_bytes),
             log.location.empty() ? "no storage" : log.location.c_str());
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

esp_err_t log_handler(httpd_req_t *req)
{
    bool keep = false;
    char query[32] = {};
    char value[8] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
            httpd_query_key_value(query, "keep", value, sizeof(value)) == ESP_OK) {
        keep = strcmp(value, "1") == 0;
    }

    const std::string version = firmware_version();
    const std::string disposition = "attachment; filename=\"kira-log-" + version + ".txt\"";
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Disposition", disposition.c_str());
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    const std::string banner = "# Kira OS " + version + " log\n";
    bool ok = httpd_resp_send_chunk(req, banner.c_str(), banner.size()) == ESP_OK;
    if (ok) {
        ok = logger::read_all([req](const char *data, size_t size) {
            return httpd_resp_send_chunk(req, data, size) == ESP_OK;
        });
    }
    if (!ok) {
        ESP_LOGW(TAG, "log download interrupted; nothing deleted");
        httpd_resp_send_chunk(req, nullptr, 0);
        return ESP_FAIL;
    }
    httpd_resp_send_chunk(req, nullptr, 0);
    if (!keep) {
        logger::clear();
        ESP_LOGI(TAG, "log downloaded and deleted from the device");
    }
    return ESP_OK;
}

void set_message(const char *message)
{
    std::lock_guard lock(s_mutex);
    s_status.message = message;
}

esp_err_t start_access_point(std::string &ssid, std::string &password)
{
    esp_err_t err = esp_wifi_get_mode(&s_previous_mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi is not initialised: %s", esp_err_to_name(err));
        return err;
    }
    if (esp_netif_get_handle_from_ifkey("WIFI_AP_DEF") == nullptr &&
            esp_netif_create_default_wifi_ap() == nullptr) {
        return ESP_FAIL;
    }

    uint8_t mac[6] = {};
    esp_efuse_mac_get_default(mac);
    char ssid_text[16];
    snprintf(ssid_text, sizeof(ssid_text), "Kira-%02X%02X", mac[4], mac[5]);
    char password_text[12];
    snprintf(password_text, sizeof(password_text), "%08lu",
             static_cast<unsigned long>(esp_random() % 100000000UL));
    ssid = ssid_text;
    password = password_text;

    wifi_mode_t mode = s_previous_mode;
    if (mode == WIFI_MODE_NULL) {
        mode = WIFI_MODE_AP;
    } else if (mode == WIFI_MODE_STA) {
        mode = WIFI_MODE_APSTA;
    }
    s_mode_changed = mode != s_previous_mode;
    if (s_mode_changed) {
        err = esp_wifi_set_mode(mode);
        if (err != ESP_OK) {
            return err;
        }
    }

    wifi_config_t config = {};
    memcpy(config.ap.ssid, ssid.c_str(), ssid.size());
    config.ap.ssid_len = static_cast<uint8_t>(ssid.size());
    memcpy(config.ap.password, password.c_str(), password.size());
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.max_connection = 2;
    config.ap.channel = 1;  // follows the station channel when connected
    err = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (err == ESP_OK && s_previous_mode == WIFI_MODE_NULL) {
        err = esp_wifi_start();
    }
    return err;
}

void restore_wifi_mode()
{
    if (s_mode_changed) {
        esp_wifi_set_mode(s_previous_mode);
        s_mode_changed = false;
    }
}

void auto_stop(void *)
{
    ESP_LOGI(TAG, "log sharing timed out");
    stop();
}

}  // namespace

esp_err_t start()
{
    {
        std::lock_guard lock(s_mutex);
        if (s_status.running) {
            return ESP_OK;
        }
    }
    std::string ssid;
    std::string password;
    esp_err_t err = start_access_point(ssid, password);
    if (err != ESP_OK) {
        restore_wifi_mode();
        set_message("Wi-Fi access point failed; see the log");
        return err;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    config.max_uri_handlers = 4;
    config.core_id = 1;
    config.lru_purge_enable = true;
    err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        restore_wifi_mode();
        set_message("HTTP server failed to start");
        return err;
    }
    const httpd_uri_t index = {.uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = nullptr};
    const httpd_uri_t log = {.uri = "/log", .method = HTTP_GET, .handler = log_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(s_server, &index);
    httpd_register_uri_handler(s_server, &log);

    if (s_stop_timer == nullptr) {
        esp_timer_create_args_t args = {};
        args.callback = auto_stop;
        args.name = "kira_logshare";
        esp_timer_create(&args, &s_stop_timer);
    }
    if (s_stop_timer != nullptr) {
        esp_timer_stop(s_stop_timer);
        esp_timer_start_once(s_stop_timer, static_cast<uint64_t>(AUTO_STOP_MINUTES) * 60 * 1000 * 1000);
    }

    std::lock_guard lock(s_mutex);
    s_status.running = true;
    s_status.ssid = ssid;
    s_status.password = password;
    s_status.ap_url = "http://192.168.4.1/log";
    s_status.lan_url = ip_url(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"));
    s_status.message = "Connect to the network and open the address";
    ESP_LOGI(TAG, "log sharing on %s", ssid.c_str());  // the password is shown on screen only
    return ESP_OK;
}

void stop()
{
    if (s_stop_timer != nullptr) {
        esp_timer_stop(s_stop_timer);
    }
    if (s_server != nullptr) {
        httpd_stop(s_server);
        s_server = nullptr;
    }
    restore_wifi_mode();
    std::lock_guard lock(s_mutex);
    s_status = Status{};
    s_status.message = "Sharing stopped";
}

Status status()
{
    std::lock_guard lock(s_mutex);
    return s_status;
}

}  // namespace kira::platform::log_share
