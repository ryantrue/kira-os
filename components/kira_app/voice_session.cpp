/* SPDX-License-Identifier: Apache-2.0 */
#include "kira/voice_session.hpp"
#include <atomic>
#include <mutex>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "brookesia/agent_openai.hpp"
#include "brookesia/service_helper/agent/manager.hpp"
#include "brookesia/service_manager/service/manager.hpp"
#include "kira/audio.hpp"
#include "kira/platform/settings.hpp"
#include "kira/surface.hpp"

namespace kira::voice {
namespace {
using Manager = esp_brookesia::service::helper::AgentManager;
using Function = Manager::FunctionId;
constexpr const char *TAG = "kira_voice";
std::mutex task_mutex;
TaskHandle_t task = nullptr;
std::atomic<uint32_t> generation{0};
// Zero means idle. A non-zero value identifies the exact activation request.
// Clearing an older request with compare_exchange must never erase a newer tap
// queued while the previous AgentManager session is still stopping.
std::atomic<uint32_t> requested_epoch{0};

bool is_requested(uint32_t epoch)
{
    return epoch != 0 && requested_epoch.load() == epoch;
}

void finish_request(uint32_t epoch)
{
    uint32_t expected = epoch;
    (void)requested_epoch.compare_exchange_strong(expected, 0);
}

void status(State state, const char *text)
{
    Surface::instance().set_status(text);
    Surface::instance().set_state(state);
}
bool action(const char *name)
{
    // Function acceptance is distinct from lifecycle completion; the worker
    // below waits for the actual manager state before exposing Listening.
    return Manager::call_function_sync(Function::TriggerGeneralAction, std::string(name)).has_value();
}
bool wait_state(const char *expected, uint32_t timeout_ms, uint32_t epoch, bool cancellable = true)
{
    for (uint32_t elapsed = 0; elapsed < timeout_ms; elapsed += 100) {
        if (cancellable && !is_requested(epoch)) return false;
        auto state = Manager::call_function_sync<std::string>(Function::GetGeneralState);
        if (state && *state == expected) return true;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return false;
}
void run(void *)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const uint32_t epoch = requested_epoch.load();
        if (epoch == 0) continue;
        auto &settings = platform::Settings::instance();
        if (audio::microphone_status().muted) {
            status(State::Error, "Open the microphone software capture gate in Settings");
            finish_request(epoch); continue;
        }
        if (settings.ai_provider() != "openai") {
            status(State::Error, "Select OpenAI in Settings: other voice providers are unavailable");
            finish_request(epoch); continue;
        }
        auto key = settings.ai_key("openai");
        auto model = settings.ai_model();
        if (key.empty() || model.empty()) {
            status(State::Error, "Set an OpenAI key and realtime model in Settings");
            finish_request(epoch); continue;
        }
        auto binding = esp_brookesia::service::ServiceManager::get_instance().bind(Manager::get_name().data());
        if (!binding.is_valid()) {
            status(State::Error, "AgentManager unavailable; see diagnostics");
            finish_request(epoch); continue;
        }
        ESP_LOGI(TAG, "activation requested: provider OpenAI, runtime model configured");
        status(State::Thinking, "Connecting to OpenAI...");
        bool configured = esp_brookesia::agent::Openai::get_instance().configure({model, key, "alloy"});
        key.clear();
        configured = configured && Manager::call_function_sync(Function::SetTargetAgent, std::string("OpenAI")).has_value();
        configured = configured && Manager::call_function_sync(Function::SetChatMode, std::string("HalfDuplex")).has_value();
        bool active = false;
        if (configured && is_requested(epoch)) {
            audio::set_session_authorized(true);
            active = action("Activate") && wait_state("Activated", 5000, epoch) &&
                     action("Start") && wait_state("Started", 40000, epoch);
        }
        if (active && is_requested(epoch)) {
            ESP_LOGI(TAG, "session started; microphone uplink enabled after explicit activation");
            status(State::Listening, "Listening - tap to end conversation");
            uint64_t last_frames = audio::response_frame_count();
            unsigned silent_ticks = 0;
            const TickType_t started_at = xTaskGetTickCount();
            while (is_requested(epoch) &&
                   static_cast<TickType_t>(xTaskGetTickCount() - started_at) < pdMS_TO_TICKS(120000)) {
                Surface::instance().set_audio_level(audio::microphone_status().level);
                const uint64_t frames = audio::response_frame_count();
                if (frames != last_frames) {
                    if (silent_ticks >= 5 || last_frames == 0) status(State::Speaking, "Kira is speaking - tap to end");
                    silent_ticks = 0;
                    last_frames = frames;
                } else if (++silent_ticks == 5) {
                    status(State::Listening, "Listening - tap to end conversation");
                }
                auto state = Manager::call_function_sync<std::string>(Function::GetGeneralState);
                if (!state || *state != "Started") {
                    active = false; break;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        audio::set_session_authorized(false);
        finish_request(epoch);
        (void)action("Stop");
        // Binding lifetime owns the complete agent teardown, including capture,
        // decoder, network task and DataFlow operation leases.
        (void)wait_state("Ready", 5000, epoch, false);
        Surface::instance().set_audio_level(0);
        if (generation == epoch) {
            if (active) status(State::Idle, "Tap to talk (session ended)");
            else status(State::Error, "Voice connection failed; check Wi-Fi, model and key");
        }
        ESP_LOGI(TAG, "session stopped; uplink gate closed");
    }
}
} // namespace
bool initialize()
{
    std::lock_guard lock(task_mutex);
    if (task) return true;
    return xTaskCreate(run, "kira_voice", 12288, nullptr, 4, &task) == pdPASS;
}
void activate()
{
    if (requested_epoch.load() != 0) { stop(); return; }
    if (!initialize()) { status(State::Error, "Voice worker unavailable"); return; }
    uint32_t epoch = ++generation;
    if (epoch == 0) epoch = ++generation;
    requested_epoch = epoch;
    xTaskNotifyGive(task);
}
void stop()
{
    // Synchronous privacy gate; no UI callback waits on network/task teardown.
    audio::set_session_authorized(false);
    requested_epoch = 0;
    ++generation;
    status(State::Idle, "Tap to talk");
}
} // namespace kira::voice
