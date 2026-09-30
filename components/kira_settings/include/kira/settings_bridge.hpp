/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "brookesia/system_core.hpp"

namespace kira::settings {

// Kira-owned settings pages hosted by the stock Brookesia Settings app.
// This bridge deliberately uses SystemApi for text input so Kira never creates
// a second LVGL keyboard.
class Bridge final {
public:
    enum class Field {
        AiModel,
        AiKey,
        AiEndpoint,
        HomeAssistantUrl,
        HomeAssistantToken,
    };

    static Bridge &instance();

    std::expected<void, std::string> request_text(
        esp_brookesia::system::core::AppContext &context,
        Field field
    );
    void cancel_keyboard(esp_brookesia::system::core::AppContext &context);

private:
    Bridge() = default;
    void handle_keyboard_result(
        Field field,
        const esp_brookesia::system::core::KeyboardResult &result
    );

    esp_brookesia::system::core::KeyboardRequestId keyboard_request_id_ =
        esp_brookesia::system::core::INVALID_KEYBOARD_REQUEST_ID;
};

} // namespace kira::settings
