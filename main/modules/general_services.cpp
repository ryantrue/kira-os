/* SPDX-License-Identifier: Apache-2.0 */
#include <utility>
#include "sdkconfig.h"
#include "brookesia/hal_adaptor.hpp"
#include "brookesia/hal_interface.hpp"
#include "brookesia/service_helper/media/audio.hpp"
#include "brookesia/service_manager/service/manager.hpp"
#include "private/utils.hpp"
#include "modules/general_services.hpp"

using namespace esp_brookesia;
using AudioPlaybackHelper = service::helper::AudioPlayback;

GeneralServices &GeneralServices::get_instance()
{
    static GeneralServices services;
    return services;
}

bool GeneralServices::init()
{
    auto &manager = service::ServiceManager::get_instance();
    BROOKESIA_CHECK_FALSE_RETURN(manager.init(), false, "Service manager init failed");
    BROOKESIA_CHECK_FALSE_RETURN(configure_audio(), false, "Audio configuration failed");
    BROOKESIA_CHECK_FALSE_RETURN(manager.start(), false, "Service manager start failed");
    return true;
}

bool GeneralServices::configure_audio()
{
#if CONFIG_BROOKESIA_HAL_ADAPTOR_AUDIO_ENABLE_AUDIO_PROCESSOR_IMPL
    hal::AudioProcessorConfig config{
        .playback = {
            .player_task = {
                .core_id = 0,
                .priority = 5,
                .stack_size = 4 * 1024,
                .stack_in_ext = true,
            },
        },
        .encoder = {},
        .decoder = {},
        .afe = {
            .vad = hal::AudioProcessorAFE_VAD_Config{},
            .wakenet = hal::AudioProcessorAFE_WakeNetConfig{
                .model_partition_label = "model",
                .mn_language = "cn",
                .start_timeout_ms = 30000,
                .end_timeout_ms = 10000,
            },
        },
    };
    BROOKESIA_CHECK_FALSE_RETURN(
        hal::AudioDevice::get_instance().set_processor_config(std::move(config)),
        false, "AFE configuration failed"
    );
#endif
    return true;
}

bool GeneralServices::start_audio_services()
{
    if (!AudioPlaybackHelper::is_available()) {
        BROOKESIA_LOGW("Audio playback unavailable; continuing without speaker");
        return true;
    }
    static auto binding = service::ServiceManager::get_instance().bind(
        AudioPlaybackHelper::get_name().data()
    );
    return binding.is_valid();
}

