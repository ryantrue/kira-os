/* SPDX-License-Identifier: Apache-2.0 */
#include <array>
#include <memory>
#include <string>
#include <vector>

#include "brookesia/gui_lvgl.hpp"
#include "brookesia/system_core.hpp"
#include "kira/platform/version.hpp"
#include "kira/recents_app.hpp"
#include "lvgl.h"

namespace kira::recents_app {
namespace {
namespace core = esp_brookesia::system::core;
constexpr const char *APP_ID = "kira.recents";
constexpr size_t MAX_CARDS = 6;

const char *state_name(core::AppState state)
{
    switch (state) {
    case core::AppState::Running: return "Running";
    case core::AppState::Paused: return "Paused";
    case core::AppState::Starting: return "Starting";
    case core::AppState::Stopping: return "Stopping";
    case core::AppState::Error: return "Error";
    case core::AppState::Stopped: return "Stopped";
    default: return "Installed";
    }
}

class RecentsApp final : public core::IApp {
public:
    core::AppManifest get_manifest() const override
    {
        return {
            .id = APP_ID,
            .name = "Recent apps",
            .localized_names = {{"en", "Recent apps"}},
            .version = kira::platform::firmware_version(),
            .kind = core::AppKind::Native,
            .visible = true,
            .preload_dom = false,
            .icon_id = {},
            .supported_systems = {},
            .icon_path = {},
            .runtime_type = esp_brookesia::runtime::BackendType::Unknown,
            .app_path = {},
            .entry = {},
            .resource_dir = {},
            .arguments = {},
        };
    }

    std::expected<void, std::string> on_start(core::AppContext &context) override
    {
        context_ = &context;
        esp_brookesia::gui::lvgl::lock_thread();
        create_locked();
        refresh_locked();
        esp_brookesia::gui::lvgl::unlock_thread();
        return root_ ? std::expected<void, std::string>{} : std::unexpected("Failed to create Recents surface");
    }

    std::expected<void, std::string> on_pause(core::AppContext &) override
    {
        set_hidden(true);
        return {};
    }

    std::expected<void, std::string> on_resume(core::AppContext &) override
    {
        esp_brookesia::gui::lvgl::lock_thread();
        if (root_) {
            lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(root_);
            refresh_locked();
        }
        esp_brookesia::gui::lvgl::unlock_thread();
        return {};
    }

    std::expected<void, std::string> on_stop(core::AppContext &) override
    {
        esp_brookesia::gui::lvgl::lock_thread();
        if (root_) lv_obj_delete(root_);
        root_ = nullptr;
        esp_brookesia::gui::lvgl::unlock_thread();
        context_ = nullptr;
        return {};
    }

private:
    struct Card {
        RecentsApp *self = nullptr;
        core::AppId app_id = core::INVALID_APP_ID;
    };

    void set_hidden(bool hidden)
    {
        esp_brookesia::gui::lvgl::lock_thread();
        if (root_) {
            if (hidden) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        }
        esp_brookesia::gui::lvgl::unlock_thread();
    }

    static void on_close(lv_event_t *event)
    {
        auto *self = static_cast<RecentsApp *>(lv_event_get_user_data(event));
        if (self && self->context_) {
            (void)self->context_->system_service().request_close_app(self->context_->app_id());
        }
    }

    static void on_card(lv_event_t *event)
    {
        auto *card = static_cast<Card *>(lv_event_get_user_data(event));
        if (!card || !card->self || !card->self->context_) return;
        // SystemApi intentionally owns lifecycle transitions. start_app is safe
        // for stopped/installed apps; paused-app resume needs a public Core API
        // hook before gesture integration is enabled.
        const auto apps = card->self->context_->system_service().list_apps();
        for (const auto &app : apps) {
            if (app.app_id != card->app_id) continue;
            if (app.state == core::AppState::Installed || app.state == core::AppState::Stopped ||
                    app.state == core::AppState::Error) {
                (void)card->self->context_->system_service().start_app(app.app_id);
            }
            return;
        }
    }

    void create_locked()
    {
        root_ = lv_obj_create(lv_display_get_layer_top(lv_display_get_default()));
        lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(root_, lv_color_hex(0x080B12), 0);
        lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

        auto *title = lv_label_create(root_);
        lv_label_set_text(title, "Recent apps");
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

        auto *close = lv_button_create(root_);
        lv_obj_align(close, LV_ALIGN_TOP_LEFT, 18, 18);
        lv_obj_add_event_cb(close, on_close, LV_EVENT_CLICKED, this);
        auto *close_label = lv_label_create(close);
        lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
        lv_obj_center(close_label);

        list_ = lv_obj_create(root_);
        lv_obj_set_size(list_, LV_PCT(92), 590);
        lv_obj_align(list_, LV_ALIGN_BOTTOM_MID, 0, -18);
        lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(list_, 10, 0);

        for (size_t i = 0; i < MAX_CARDS; ++i) {
            cards_[i] = {this, core::INVALID_APP_ID};
            buttons_[i] = lv_button_create(list_);
            lv_obj_set_width(buttons_[i], LV_PCT(100));
            lv_obj_set_height(buttons_[i], 78);
            lv_obj_add_event_cb(buttons_[i], on_card, LV_EVENT_CLICKED, &cards_[i]);
            labels_[i] = lv_label_create(buttons_[i]);
            lv_obj_align(labels_[i], LV_ALIGN_LEFT_MID, 12, 0);
        }
    }

    void refresh_locked()
    {
        if (!context_) return;
        std::vector<core::AppInfo> candidates;
        for (const auto &app : context_->system_service().list_apps()) {
            if (!app.manifest.visible || app.manifest.id == APP_ID) continue;
            // Core 0.8.x exposes lifecycle state but no launch timestamp/MRU.
            // Prefer live lifecycle entries now; true bounded MRU will be wired
            // through System Super's app-start hook in the gesture integration.
            if (app.state == core::AppState::Running || app.state == core::AppState::Paused) {
                candidates.push_back(app);
            }
        }
        for (const auto &app : context_->system_service().list_apps()) {
            if (candidates.size() >= MAX_CARDS) break;
            if (!app.manifest.visible || app.manifest.id == APP_ID) continue;
            if (app.state != core::AppState::Running && app.state != core::AppState::Paused) {
                candidates.push_back(app);
            }
        }

        for (size_t i = 0; i < MAX_CARDS; ++i) {
            if (i >= candidates.size()) {
                cards_[i].app_id = core::INVALID_APP_ID;
                lv_obj_add_flag(buttons_[i], LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            const auto &app = candidates[i];
            cards_[i].app_id = app.app_id;
            std::string text = core::resolve_app_display_name(app.manifest);
            text += "   ·   ";
            text += state_name(app.state);
            if (app.state == core::AppState::Paused) text += "   ·   resume hook pending";
            lv_label_set_text(labels_[i], text.c_str());
            lv_obj_clear_flag(buttons_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    core::AppContext *context_ = nullptr;
    lv_obj_t *root_ = nullptr;
    lv_obj_t *list_ = nullptr;
    std::array<lv_obj_t *, MAX_CARDS> buttons_{};
    std::array<lv_obj_t *, MAX_CARDS> labels_{};
    std::array<Card, MAX_CARDS> cards_{};
};

class Provider final : public core::IAppProvider {
public:
    core::AppManifest get_manifest() const override { return RecentsApp().get_manifest(); }
    std::shared_ptr<core::IApp> create_app() override { return std::make_shared<RecentsApp>(); }
};

BROOKESIA_SYSTEM_CORE_APP_PROVIDER_REGISTER_WITH_SYMBOL(
    Provider,
    APP_ID,
    kira_recents_app_provider_symbol
);
} // namespace

void ensure_linked() {}

} // namespace kira::recents_app
