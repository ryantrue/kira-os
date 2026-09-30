/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <array>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include "brookesia/system_core.hpp"

namespace kira::settings {

// Controller for Kira pages inside the one stock Settings app. GUI callbacks
// only read cached platform state or submit jobs; all text uses SystemApi.
class Bridge final {
public:
    enum class Field { AiModel, AiKey, AiEndpoint, HomeAssistantUrl, HomeAssistantToken };
    static Bridge &instance();
    std::expected<void, std::string> start(esp_brookesia::system::core::AppContext &context);
    void stop(esp_brookesia::system::core::AppContext &context);
    void set_active(esp_brookesia::system::core::AppContext &context, bool active);
    std::expected<void, std::string> action(esp_brookesia::system::core::AppContext &context, std::string_view action);
    std::expected<void, std::string> poll(esp_brookesia::system::core::AppContext &context);
    std::expected<void, std::string> request_text(esp_brookesia::system::core::AppContext &context, Field field);
    void cancel_keyboard(esp_brookesia::system::core::AppContext &context);
    static constexpr std::string_view TIMER_NAME = "kira.settings.poll";

private:
    Bridge() = default;
    void handle_keyboard_result(Field field, std::string_view provider,
        const esp_brookesia::system::core::KeyboardResult &result);
    std::expected<void, std::string> confirm(esp_brookesia::system::core::AppContext &context,
        std::string text, std::string detail, std::function<void()> accepted);
    esp_brookesia::system::core::AppContext *app_context_ = nullptr;
    esp_brookesia::system::core::KeyboardRequestId keyboard_request_id_ = esp_brookesia::system::core::INVALID_KEYBOARD_REQUEST_ID;
    esp_brookesia::system::core::MessageDialogRequestId dialog_request_id_ = esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID;
    esp_brookesia::system::core::TimerId timer_id_ = esp_brookesia::system::core::INVALID_TIMER_ID;
    bool active_ = false;
    size_t entity_page_ = 0;
    std::array<std::string, 8> entity_ids_{};
    std::string notice_;
};

} // namespace kira::settings
