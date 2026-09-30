/* SPDX-License-Identifier: Apache-2.0 */
#include "kira/settings_bridge.hpp"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <vector>
#include "esp_err.h"
#include "kira/audio.hpp"
#include "kira/platform/ai_providers.hpp"
#include "kira/platform/hardware.hpp"
#include "kira/platform/home_assistant.hpp"
#include "kira/platform/log_share.hpp"
#include "kira/platform/logger.hpp"
#include "kira/platform/sdcard.hpp"
#include "kira/platform/settings.hpp"
#include "kira/platform/updater.hpp"
#include "kira/platform/version.hpp"
#include "kira/platform/worker.hpp"

namespace kira::settings {
namespace {
namespace core = esp_brookesia::system::core;
namespace gui = esp_brookesia::gui;
namespace platform = kira::platform;
using platform::Settings;

std::string bytes(uint64_t value)
{
    char text[48];
    snprintf(text, sizeof(text), "%.1f MiB", static_cast<double>(value) / (1024 * 1024));
    return text;
}

core::KeyboardRequestOptions make_options(Bridge::Field field)
{
    auto &settings = Settings::instance();
    core::KeyboardRequestOptions options;
    options.max_length = 512;
    options.mode = "text";
    switch (field) {
    case Bridge::Field::AiModel:
        options.title = "AI model";
        options.placeholder = "Realtime model name";
        options.initial_text = settings.ai_model();
        options.max_length = 128;
        break;
    case Bridge::Field::AiKey:
        options.title = "AI API key";
        options.placeholder = settings.has_ai_key(settings.ai_provider()) ? "saved (enter to replace)" : "not set";
        options.password = true;
        break;
    case Bridge::Field::AiEndpoint:
        options.title = "AI endpoint";
        options.initial_text = settings.ai_endpoint();
        break;
    case Bridge::Field::HomeAssistantUrl:
        options.title = "Home Assistant address";
        options.placeholder = "http://homeassistant.local:8123";
        options.initial_text = settings.ha_url();
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

Bridge &Bridge::instance() { static Bridge bridge; return bridge; }

std::expected<void, std::string> Bridge::start(core::AppContext &context)
{
    stop(context);
    context_ = &context;
    std::vector<std::string> actions;
    for (const auto *name : {"autostart", "idle", "open", "tone", "provider", "model", "key",
             "ha_url", "ha_token", "ha_test", "ha_refresh", "ha_prev", "ha_next", "sd_refresh", "sd_format",
             "log_enable", "log_share", "log_clear", "update_check", "update_install", "mic_test", "mic_mute",
             "mic_gain_down", "mic_gain_up"}) {
        actions.push_back(std::string("settings.kira.") + name);
    }
    for (size_t i = 0; i < entity_ids_.size(); ++i) actions.push_back("settings.kira.entity" + std::to_string(i));
    return context.gui().subscribe_actions(actions);
}

void Bridge::stop(core::AppContext &context)
{
    active_ = false;
    context_ = nullptr; // Guard callbacks before cancelling shell requests.
    cancel_keyboard(context);
    if (dialog_request_id_ != core::INVALID_MESSAGE_DIALOG_REQUEST_ID) {
        (void)context.system_service().hide_message_dialog(dialog_request_id_);
        dialog_request_id_ = core::INVALID_MESSAGE_DIALOG_REQUEST_ID;
    }
    if (timer_id_ != core::INVALID_TIMER_ID) {
        (void)context.timer().stop(timer_id_);
        timer_id_ = core::INVALID_TIMER_ID;
    }
    entity_ids_ = {};
    entity_page_ = 0;
    notice_.clear();
}

void Bridge::set_active(core::AppContext &context, bool active)
{
    active_ = active;
    if (!active) {
        cancel_keyboard(context);
        if (timer_id_ != core::INVALID_TIMER_ID) (void)context.timer().stop(timer_id_);
        timer_id_ = core::INVALID_TIMER_ID;
        return;
    }
    if (timer_id_ == core::INVALID_TIMER_ID) {
        auto timer = context.timer().start_periodic(TIMER_NAME, 1000);
        if (timer) timer_id_ = *timer;
    }
    platform::sdcard::refresh_async();
    (void)poll(context);
}

std::expected<void, std::string> Bridge::request_text(core::AppContext &context, Field field)
{
    cancel_keyboard(context);
    const auto provider = Settings::instance().ai_provider();
    auto request = context.system_service().show_keyboard(make_options(field),
        [this, field, provider](const core::KeyboardResult &result) {
            if (context_ != nullptr && result.request_id == keyboard_request_id_) {
                handle_keyboard_result(field, provider, result);
            }
        });
    if (!request) return std::unexpected(request.error());
    keyboard_request_id_ = *request;
    return {};
}

void Bridge::cancel_keyboard(core::AppContext &context)
{
    const auto id = keyboard_request_id_;
    keyboard_request_id_ = core::INVALID_KEYBOARD_REQUEST_ID;
    if (id != core::INVALID_KEYBOARD_REQUEST_ID) (void)context.system_service().hide_keyboard(id);
}

void Bridge::handle_keyboard_result(Field field, std::string_view provider, const core::KeyboardResult &result)
{
    keyboard_request_id_ = core::INVALID_KEYBOARD_REQUEST_ID;
    if (!result.confirmed) return;
    auto &settings = Settings::instance();
    esp_err_t err = ESP_OK;
    switch (field) {
    case Field::AiModel: err = settings.set_ai_model(result.text); break;
    case Field::AiKey:
        // Empty confirmation intentionally leaves the existing secret intact.
        if (!result.text.empty()) err = settings.set_ai_key(provider, result.text);
        break;
    case Field::AiEndpoint: err = settings.set_ai_endpoint(result.text); break;
    case Field::HomeAssistantUrl: err = settings.set_ha_url(result.text); break;
    case Field::HomeAssistantToken:
        if (!result.text.empty()) err = settings.set_ha_token(result.text);
        break;
    }
    notice_ = err == ESP_OK ? "Setting saved. New AI settings apply to the next conversation."
                            : "Setting was rejected; check the value.";
    if (context_ != nullptr) (void)poll(*context_);
}

std::expected<void, std::string> Bridge::confirm(core::AppContext &context, std::string text,
    std::string detail, std::function<void()> accepted)
{
    if (dialog_request_id_ != core::INVALID_MESSAGE_DIALOG_REQUEST_ID) return {};
    auto request = context.system_service().show_message_dialog({
        .text = std::move(text), .informative_text = std::move(detail), .icon = core::MessageDialogIcon::Warning,
        .buttons = {{"Cancel", core::MessageDialogButtonRole::Reject}, {"Confirm", core::MessageDialogButtonRole::Destructive}},
        .auto_close_ms = 0,
    }, [this, accepted = std::move(accepted)](const core::MessageDialogResult &result) {
        if (result.request_id != dialog_request_id_) return;
        dialog_request_id_ = core::INVALID_MESSAGE_DIALOG_REQUEST_ID;
        if (context_ != nullptr && result.button_role == core::MessageDialogButtonRole::Destructive) accepted();
    });
    if (!request) return std::unexpected(request.error());
    dialog_request_id_ = *request;
    return {};
}

std::expected<void, std::string> Bridge::action(core::AppContext &context, std::string_view action)
{
    auto &settings = Settings::instance();
    if (action == "settings.kira.model") return request_text(context, Field::AiModel);
    if (action == "settings.kira.key") return request_text(context, Field::AiKey);
    if (action == "settings.kira.ha_url") return request_text(context, Field::HomeAssistantUrl);
    if (action == "settings.kira.ha_token") return request_text(context, Field::HomeAssistantToken);
    if (action == "settings.kira.autostart") (void)settings.set_autostart(!settings.autostart());
    else if (action == "settings.kira.idle") {
        const auto &choices = Settings::IDLE_CHOICES_MIN;
        const auto it = std::find(choices.begin(), choices.end(), settings.idle_minutes());
        (void)settings.set_idle_minutes(it == choices.end() || std::next(it) == choices.end() ? choices.front() : *std::next(it));
    } else if (action == "settings.kira.open") {
        for (const auto &app : context.system_service().list_apps()) {
            if (app.manifest.id == "kira.assistant") return context.system_service().start_app(app.app_id);
        }
        notice_ = "Kira app is unavailable.";
    } else if (action == "settings.kira.tone") {
        notice_ = kira::audio::play_test_tone() ? "Playing speaker test tone." : "Speaker test could not start.";
    } else if (action == "settings.kira.provider") {
        // Only providers with an implemented realtime adapter can be selected.
        (void)settings.set_ai_provider("openai");
    } else if (action == "settings.kira.ha_test") platform::home_assistant::test_async();
    else if (action == "settings.kira.ha_refresh") platform::home_assistant::refresh_async();
    else if (action == "settings.kira.ha_prev") { if (entity_page_ > 0) --entity_page_; }
    else if (action == "settings.kira.ha_next") ++entity_page_;
    else if (action.starts_with("settings.kira.entity")) {
        const auto suffix = action.substr(std::string_view("settings.kira.entity").size());
        if (suffix.size() == 1 && suffix.front() >= '0' && suffix.front() <= '7') {
            const auto &id = entity_ids_[suffix.front() - '0'];
            if (!id.empty()) platform::home_assistant::toggle_async(id);
        }
    } else if (action == "settings.kira.sd_refresh") platform::sdcard::refresh_async();
    else if (action == "settings.kira.sd_format") {
        return confirm(context, "Erase and format the removable SD card?", "All SD files, logs and recovery backups will be deleted. Internal LittleFS is not formatted.", [] { platform::sdcard::format_async(); });
    } else if (action == "settings.kira.log_enable") platform::logger::set_enabled(!platform::logger::enabled());
    else if (action == "settings.kira.log_share") {
        (void)platform::submit_job("log_share", [] {
            if (platform::log_share::status().running) platform::log_share::stop();
            else (void)platform::log_share::start();
        });
    } else if (action == "settings.kira.log_clear") {
        return confirm(context, "Delete saved diagnostics?", "This deletes the saved log on this device.", [] {
            (void)platform::submit_job("log_clear", [] { platform::logger::clear(); });
        });
    } else if (action == "settings.kira.update_check") platform::updater::check_async();
    else if (action == "settings.kira.update_install") {
        const auto status = platform::updater::status();
        if (status.phase != platform::updater::Phase::Available) return {};
        return confirm(context, "Install Kira OS " + status.latest + "?", "The image is verified with SHA-256, then installed by recovery. Keep power connected. A mounted SD card is required.", [] { platform::updater::install_async(); });
    } else if (action == "settings.kira.mic_mute") {
        const auto mic = kira::audio::microphone_status();
        kira::audio::set_microphone_muted(!mic.muted);
        notice_ = mic.muted ? "Microphone software capture gate opened."
                            : "Microphone software capture gate closed.";
    } else if (action == "settings.kira.mic_test") {
        notice_ = kira::audio::start_microphone_test()
            ? "Recording three seconds locally, then replaying it. No audio is uploaded."
            : "Microphone test could not start; microphone is muted, unavailable, or busy.";
    } else if (action == "settings.kira.mic_gain_down" || action == "settings.kira.mic_gain_up") {
        const auto mic = kira::audio::microphone_status();
        const float delta = action.ends_with("gain_up") ? 3.0F : -3.0F;
        const float target = std::clamp(mic.gain + delta, 0.0F, 37.5F);
        if (!platform::submit_job("mic_gain", [target] { (void)kira::audio::set_microphone_gain(target); })) {
            notice_ = "Microphone gain change is already in progress.";
        }
    }
    return poll(context);
}

std::expected<void, std::string> Bridge::poll(core::AppContext &context)
{
    if (!active_) return {};
    auto &settings = Settings::instance();
    std::vector<gui::BindingValueUpdate> updates;
    const auto text = [&updates](std::string path, std::string value) {
        updates.push_back({.absolute_path = "/kira/page/" + path, .key = "labelProps.text", .value = std::move(value)});
    };
    const auto flag = [&updates](std::string path, const char *key, bool value) {
        updates.push_back({.absolute_path = "/kira/page/" + path, .key = key, .value = value ? "true" : "false"});
    };
    text("notice", notice_);
    text("autostart/title", std::string("Start Kira after inactivity: ") + (settings.autostart() ? "On" : "Off"));
    text("idle/title", "Inactivity timeout: " + std::to_string(settings.idle_minutes()) + " minutes");
    flag("idle", "commonProps.disabled", !settings.autostart());
    text("provider/title", "Voice provider: " + settings.ai_provider() + " (only available adapter)");
    text("model/title", "Realtime model: " + settings.ai_model());
    text("key/title", std::string("API key: ") + (settings.has_ai_key(settings.ai_provider()) ? "saved (hidden)" : "not set"));
    text("ha_url/title", "Address: " + settings.ha_url());
    text("ha_token/title", std::string("HA token: ") + (settings.has_ha_token() ? "saved (hidden)" : "not set"));
    const auto ha = platform::home_assistant::status();
    text("ha_status", std::string(ha.busy ? "Working... " : ha.connected ? "Connected. " : "Disconnected. ") + ha.message);
    const auto pages = std::max<size_t>(1, (ha.entities.size() + entity_ids_.size() - 1) / entity_ids_.size());
    entity_page_ = std::min(entity_page_, pages - 1);
    entity_ids_ = {};
    for (size_t i = 0; i < entity_ids_.size(); ++i) {
        const auto index = entity_page_ * entity_ids_.size() + i;
        const auto id = "entity" + std::to_string(i);
        flag(id, "commonProps.hidden", index >= ha.entities.size());
        if (index < ha.entities.size()) {
            const auto &entity = ha.entities[index];
            entity_ids_[i] = entity.entity_id;
            text(id + "/title", entity.name + ": " + entity.state + " (toggle)");
            flag(id, "commonProps.disabled", ha.busy || (entity.state != "on" && entity.state != "off"));
        }
    }
    text("ha_page", "Entities page " + std::to_string(entity_page_ + 1) + "/" + std::to_string(pages));
    flag("ha_prev", "commonProps.disabled", entity_page_ == 0);
    flag("ha_next", "commonProps.disabled", entity_page_ + 1 >= pages);
    flag("ha_test", "commonProps.disabled", ha.busy);
    flag("ha_refresh", "commonProps.disabled", ha.busy);
    const auto sd = platform::sdcard::status();
    text("sd_status", sd.mounted ? "Present / mounted: " + sd.filesystem + "\nCapacity " + bytes(sd.total_bytes) + ", free " + bytes(sd.free_bytes) + ", used " + bytes(sd.total_bytes - std::min(sd.total_bytes, sd.free_bytes)) + "\n" + sd.message : "No mounted SD card. " + sd.message);
    flag("sd_format", "commonProps.disabled", sd.busy);
    flag("sd_refresh", "commonProps.disabled", sd.busy);
    const auto log = platform::logger::status();
    text("log_enable/title", std::string("Save diagnostic logs: ") + (log.enabled ? "On" : "Off"));
    text("log_status", log.location + "\nStored " + bytes(log.stored_bytes) + "; dropped lines " + std::to_string(log.dropped_lines));
    const auto share = platform::log_share::status();
    text("log_share/title", share.running ? "Stop sharing logs" : "Share logs over Wi-Fi");
    text("share_status", share.running ? "Wi-Fi: " + share.ssid + "\nPassword: " + share.password + "\n" + share.ap_url + "\n" + share.lan_url : share.message);
    const auto update = platform::updater::status();
    text("version", "Current: " + platform::firmware_version() + "\nAvailable: " + (update.latest.empty() ? "not checked" : update.latest));
    text("update_status", update.message + (update.total_bytes ? "\n" + bytes(update.download_bytes) + " / " + bytes(update.total_bytes) : ""));
    flag("update_install", "commonProps.disabled", update.phase != platform::updater::Phase::Available || !sd.mounted);
    const bool updating = update.phase == platform::updater::Phase::Checking || update.phase == platform::updater::Phase::Downloading || update.phase == platform::updater::Phase::Verifying || update.phase == platform::updater::Phase::ReadyToInstall;
    flag("update_check", "commonProps.disabled", updating);
    const auto mic = kira::audio::microphone_status();
    char mic_summary[160];
    snprintf(mic_summary, sizeof(mic_summary), "%s | %lu Hz | %u channels\nLevel %.0f%% | gain %.1f dB\n%s",
             mic.available ? "Available" : "Unavailable", static_cast<unsigned long>(mic.sample_rate),
             static_cast<unsigned>(mic.channels), mic.level * 100.0F, mic.gain, mic.test_state.c_str());
    text("mic_status", mic_summary);
    text("mic_mute/title", mic.muted ? "Open microphone software capture gate"
                                      : "Close microphone software capture gate");
    text("mic_gain_down/title", "Reduce microphone gain (current " + std::to_string(static_cast<int>(mic.gain)) + " dB)");
    text("mic_gain_up/title", "Increase microphone gain (current " + std::to_string(static_cast<int>(mic.gain)) + " dB)");
    flag("mic_mute", "commonProps.disabled", !mic.available || mic.running);
    flag("mic_gain_down", "commonProps.disabled", !mic.available || mic.running || mic.gain <= 0.0F);
    flag("mic_gain_up", "commonProps.disabled", !mic.available || mic.running || mic.gain >= 37.5F);
    flag("mic_test", "commonProps.disabled", !mic.available || mic.running || mic.muted);
    const auto network = platform::hardware::connectivity_status();
    text("connectivity_status", network.wifi_message + "\n" + network.bluetooth_message + "\nWi-Fi controls are in Settings > Wi-Fi.");
    std::string pins;
    for (const auto &pin : platform::hardware::gpio_status()) {
        pins += "GPIO " + std::to_string(pin.number) + " | " + pin.function + " | " + pin.direction + " | " + pin.state + " | " + (pin.reserved ? "Reserved/System" : "Unqualified: read-only") + "\n";
    }
    text("gpio_status", std::move(pins));
    return context.gui().set_binding_values(updates);
}
} // namespace kira::settings
