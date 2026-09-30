/* SPDX-License-Identifier: Apache-2.0 */

#include <memory>
#include <string>

#include "brookesia/system_core.hpp"
#include "kira/app.hpp"
#include "kira/platform/version.hpp"
#include "kira/surface.hpp"
#include "kira/voice_session.hpp"

namespace kira::app {
namespace {

namespace core = esp_brookesia::system::core;

constexpr const char *APP_ID = "kira.assistant";
constexpr const char *SETTINGS_APP_ID = "brookesia.general.settings";

class KiraApp final : public core::IApp {
public:
    core::AppManifest get_manifest() const override
    {
        return {
            .id = APP_ID,
            .name = "Kira",
            .localized_names = {{"en", "Kira"}},
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
        auto &surface = kira::Surface::instance();
        surface.set_close_handler([this] {
            if (context_ != nullptr) {
                (void)context_->system_service().request_close_app(context_->app_id());
            }
        });
        surface.set_settings_handler([this] {
            if (context_ == nullptr) return;
            for (const auto &app : context_->system_service().list_apps()) {
                if (app.manifest.id == SETTINGS_APP_ID) {
                    (void)context_->system_service().start_app(app.app_id);
                    return;
                }
            }
        });
        surface.set_tap_handler([] { kira::voice::activate(); });
        if (!surface.start()) {
            context_ = nullptr;
            return std::unexpected("Failed to create Kira surface");
        }
        if (!kira::voice::initialize()) {
            surface.stop();
            context_ = nullptr;
            return std::unexpected("Failed to create Kira voice worker");
        }
        return {};
    }

    std::expected<void, std::string> on_pause(core::AppContext &) override
    {
        kira::voice::stop();
        kira::Surface::instance().pause();
        return {};
    }

    std::expected<void, std::string> on_resume(core::AppContext &) override
    {
        kira::Surface::instance().resume();
        return {};
    }

    std::expected<void, std::string> on_stop(core::AppContext &) override
    {
        kira::voice::stop();
        kira::Surface::instance().stop();
        context_ = nullptr;
        return {};
    }

private:
    core::AppContext *context_ = nullptr;
};

class KiraAppProvider final : public core::IAppProvider {
public:
    core::AppManifest get_manifest() const override
    {
        return KiraApp().get_manifest();
    }

    std::shared_ptr<core::IApp> create_app() override
    {
        return std::make_shared<KiraApp>();
    }
};

BROOKESIA_SYSTEM_CORE_APP_PROVIDER_REGISTER_WITH_SYMBOL(
    KiraAppProvider,
    APP_ID,
    kira_app_provider_symbol
);

} // namespace

void ensure_linked() {}

} // namespace kira::app
