/* SPDX-License-Identifier: Apache-2.0 */
#include "http_util.hpp"

#include <cstdlib>
#include <strings.h>

#include "esp_crt_bundle.h"
#include "esp_log.h"

namespace kira::platform::detail {
namespace {

constexpr const char *TAG = "kira_http";

struct StreamContext {
    const std::function<bool(const char *, size_t, size_t)> *sink = nullptr;
    size_t total = 0;
    bool aborted = false;
};

bool is_success(int status)
{
    return status >= 200 && status < 300;
}

esp_err_t on_event(esp_http_client_event_t *event)
{
    auto *context = static_cast<StreamContext *>(event->user_data);
    if (context == nullptr || context->aborted) {
        return ESP_OK;
    }
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key != nullptr &&
            strcasecmp(event->header_key, "Content-Length") == 0 && event->header_value != nullptr) {
        context->total = strtoul(event->header_value, nullptr, 10);
    }
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        // Redirect bodies (3xx) arrive here too; only keep the final 2xx body.
        if (!is_success(esp_http_client_get_status_code(event->client))) {
            return ESP_OK;
        }
        if (!(*context->sink)(static_cast<const char *>(event->data), static_cast<size_t>(event->data_len),
                              context->total)) {
            context->aborted = true;
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

esp_http_client_handle_t open_client(const HttpRequest &request, StreamContext *context)
{
    esp_http_client_config_t config = {};
    config.url = request.url.c_str();
    config.method = request.method;
    config.timeout_ms = request.timeout_ms;
    config.event_handler = on_event;
    config.user_data = context;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.buffer_size = 4096;
    config.buffer_size_tx = 2048;
    config.max_redirection_count = 5;
    config.disable_auto_redirect = !request.follow_redirects;
    config.user_agent = "kira-os";
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return nullptr;
    }
    for (const auto &[name, value] : request.headers) {
        esp_http_client_set_header(client, name.c_str(), value.c_str());
    }
    if (!request.body.empty()) {
        esp_http_client_set_post_field(client, request.body.c_str(), static_cast<int>(request.body.size()));
    }
    return client;
}

}  // namespace

esp_err_t http_stream(const HttpRequest &request,
                      const std::function<bool(const char *data, size_t size, size_t total)> &sink,
                      int &status)
{
    StreamContext context;
    context.sink = &sink;
    esp_http_client_handle_t client = open_client(request, &context);
    if (client == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = esp_http_client_perform(client);
    status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (context.aborted) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK && !is_success(status)) {
        err = ESP_ERR_HTTP_BASE;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "request failed: status %d, %s", status, esp_err_to_name(err));
    }
    return err;
}

HttpResponse http_fetch(const HttpRequest &request, size_t max_body)
{
    HttpResponse response;
    auto sink = [&response, max_body](const char *data, size_t size, size_t) {
        if (response.body.size() + size > max_body) {
            response.truncated = true;
            return false;
        }
        response.body.append(data, size);
        return true;
    };
    response.err = http_stream(request, sink, response.status);
    return response;
}

}  // namespace kira::platform::detail
