/* SPDX-License-Identifier: Apache-2.0 */

#include "kira/settings_bridge.hpp"

#include "kira/platform/settings.hpp"

namespace kira::settings {
namespace {

namespace core = esp_brookesia::system::core;
using kira::platform::Settings;

core::KeyboardRequestOptions make_options(Bridge::Field field)
{
    auto &settings = Settings::instance();
    core::KeyboardRequestOptions options;
    options.max_length = 512;
    options.mode = "text";

    switch (field) {
    case Bridge::Field::AiModel:
        options.title = "AI model";
        options.placeholder = "model name";
        options.initial_text = settings.ai_model();
        options.max_length = 128;
        break;
    case Bridge::Field::AiKey:
        options.title = "AI API key";
        options.placeholder = settings.has_ai_key(settings.ai_provider()) ? "saved (enter to replace)" : "not set";
        options.password = true;
        options.max_length = 512;
        break;
    case Bridge::Field::AiEndpoint:
        options.title = "AI endpoint";
        options.placeholder = "http://kira-brain.local:8765";
        options.initial_text = settings.ai_endpoint();
        options.max_length = 512;
        break;
    case Bridge::Field::HomeAssistantUrl:
        options.title = "Home Assistant address";
        options.placeholder = "http://homeassistant.local:8123";
        options.initial_text = settings.ha_url();
        options.max_length = 512;
        break;
    case Bridge::Field::HomeAssistantToken:
        options.title = "Home Assistant token";
        options.placeholder = settings.has_ha_token() ? "saved (enter to replace)" : "not set";
        options.password = true;
        options.max_length = 1024;
        break;
    }
    return options;
}

} // namespace

Bridge &Bridge::instance()
{
    static Bridge bridge;
    return bridge;
}

std::expected<void, std::string> Bridge::request_text(core::AppContext &context, Field field)
{
    cancel_keyboard(context);
    auto request = context.system_service().show_keyboard(
        make_options(field),
        [this, field](const core::KeyboardResult &result) {
            handle_keyboard_result(field, result);
        }
    );
    if (!request) {
        return std::unexpected(request.error());
    }
    keyboard_request_id_ = *request;
    return {};
}

void Bridge::cancel_keyboard(core::AppContext &context)
{
    if (keyboard_request_id_ == core::INVALID_KEYBOARD_REQUEST_ID) {
        return;
    }
    (void)context.system_service().hide_keyboard(keyboard_request_id_);
    keyboard_request_id_ = core::INVALID_KEYBOARD_REQUEST_ID;
}

void Bridge::handle_keyboard_result(Field field, const core::KeyboardResult &result)
{
    keyboard_request_id_ = core::INVALID_KEYBOARD_REQUEST_ID;
    if (!result.confirmed) {
        return;
    }

    auto &settings = Settings::instance();
    switch (field) {
    case Field::AiModel:
        (void)settings.set_ai_model(result.text);
        break;
    case Field::AiKey:
        // Empty confirmation intentionally leaves the existing secret intact.
        if (!result.text.empty()) {
            (void)settings.set_ai_key(settings.ai_provider(), result.text);
        }
        break;
    case Field::AiEndpoint:
        (void)settings.set_ai_endpoint(result.text);
        break;
    case Field::HomeAssistantUrl:
        (void)settings.set_ha_url(result.text);
        break;
    case Field::HomeAssistantToken:
        if (!result.text.empty()) {
            (void)settings.set_ha_token(result.text);
        }
        break;
    }
}

} // namespace kira::settings
