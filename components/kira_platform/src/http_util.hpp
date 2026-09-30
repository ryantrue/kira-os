/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "esp_err.h"
#include "esp_http_client.h"

namespace kira::platform::detail {

struct HttpRequest {
    std::string url;
    esp_http_client_method_t method = HTTP_METHOD_GET;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    int timeout_ms = 15000;
    bool follow_redirects = true;
};

struct HttpResponse {
    esp_err_t err = ESP_FAIL;
    int status = 0;
    std::string body;
    bool truncated = false;
};

// Buffers the response body (up to max_body bytes). Follows redirects.
// HTTPS uses the ESP-IDF certificate bundle.
HttpResponse http_fetch(const HttpRequest &request, size_t max_body);

// Streams a 2xx response body to `sink`; `sink` returning false aborts.
// `total` receives Content-Length when known (0 otherwise).
esp_err_t http_stream(const HttpRequest &request,
                      const std::function<bool(const char *data, size_t size, size_t total)> &sink,
                      int &status);

}  // namespace kira::platform::detail
