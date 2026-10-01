/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include <memory>

#include "boost/thread.hpp"
#include "brookesia/gui_lvgl.hpp"
#include "brookesia/lib_utils/thread_config.hpp"
#include "brookesia/system_super.hpp"

#include "kira/boot.hpp"
#include "kira/app.hpp"
#include "kira/home_assistant_app.hpp"
#include "kira/platform/logger.hpp"
#include "kira/recents_app.hpp"
#include "kira/scripts_app.hpp"
#include "kira/platform/settings.hpp"
#include "kira/platform/version.hpp"
#include "modules/display.hpp"
#include "modules/general_services.hpp"
#include "private/utils.hpp"

using namespace esp_brookesia;

extern "C" void app_main(void)
{
    kira::boot::start();  // boot safety first: NVS, rollback confirmation, crash counter
    kira::platform::Settings::instance().load();
    kira::platform::logger::start();
    auto setup = []() {
        BROOKESIA_LOGI("Starting Kira OS %1%", kira::platform::firmware_version());
        BROOKESIA_CHECK_FALSE_EXIT(
            GeneralServices::get_instance().init(), "Failed to initialize services"
        );

        auto &display = Display::get_instance();
        BROOKESIA_CHECK_FALSE_EXIT(display.start({}), "Failed to start display");
        if (!GeneralServices::get_instance().start_audio_services()) {
            BROOKESIA_LOGW("Audio services unavailable; continuing without audio");
        }

        static std::unique_ptr<system::super::System> system_instance;
        system_instance = std::make_unique<system::super::System>();

        system::super::System::Config config;
        config.core_config.gui_backend = std::make_unique<gui::lvgl::Backend>();
        config.core_config.environment = {
            .width_px = static_cast<int32_t>(display.width()),
            .height_px = static_cast<int32_t>(display.height()),
            .density = 1.0F,
            .font_scale = 1.0F,
            .language = "en_US",
            .theme_id = "dark",
        };

        auto init_result = system_instance->init(std::move(config));
        BROOKESIA_CHECK_FALSE_EXIT(
            init_result, "System init failed: %1%", init_result.error()
        );
        auto start_result = system_instance->start();
        BROOKESIA_CHECK_FALSE_EXIT(
            start_result, "System start failed: %1%", start_result.error()
        );
        const esp_err_t boot_ready = kira::boot::mark_ready();
        if (boot_ready != ESP_OK) {
            BROOKESIA_LOGW("Failed to arm stable confirmation: %1%", esp_err_to_name(boot_ready));
        }

        // Kira is registered as a native System Super application. The launcher
        // owns its start/pause/resume/stop lifecycle; no persistent top-layer
        // assistant surface is created at boot.
        kira::app::ensure_linked();
        kira::home_assistant_app::ensure_linked();
        kira::recents_app::ensure_linked();
        kira::scripts_app::ensure_linked();
        BROOKESIA_LOGI("Kira OS ready (assistant available as native app)");
    };

    BROOKESIA_THREAD_CONFIG_GUARD({
        .name = "kira_setup",
        .stack_size = 48 * 1024,
        .stack_in_ext = true,
    });
    boost::thread(setup).detach();
}
