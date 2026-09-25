#pragma once

#include <chrono>
#include "esp_err.h"

namespace kira::boot {
esp_err_t start(std::chrono::seconds stable_for = std::chrono::seconds(30));
esp_err_t confirm_now();
bool pending_verification();
esp_err_t request_install();
esp_err_t request_restore();
esp_err_t request_recovery();
}  // namespace kira::boot
