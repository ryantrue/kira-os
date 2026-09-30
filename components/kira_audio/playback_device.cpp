/* SPDX-License-Identifier: Apache-2.0 */
#include "kira/audio.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <numbers>
#include <string>
#include <vector>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "brookesia/hal_interface.hpp"
#include "brookesia/hal_interface/device.hpp"  // Device is not part of the umbrella header
#include "brookesia/hal_interface/interfaces/audio/codec_player.hpp"
#include "brookesia/hal_interface/interfaces/audio/processor.hpp"

#if !CONFIG_BROOKESIA_HAL_ADAPTOR_AUDIO_ENABLE_PROCESSOR_IMPL

namespace esp_brookesia::hal {
namespace {

constexpr const char *TAG = "kira_audio";
constexpr const char *PLAYBACK_INSTANCE = "Audio:Playback";  // PlaybackIface::get_default_instance_name()
constexpr size_t CHUNK_BYTES = 4096;

struct PcmFormat {
    uint8_t bits = 16;
    uint8_t channels = 1;
    uint32_t sample_rate = 16000;
};

uint32_t read_le32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t read_le16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

// Positions `file` at the start of PCM data. Returns false for non-PCM WAV.
bool parse_wav(FILE *file, PcmFormat &format)
{
    uint8_t header[12];
    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
            memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        return false;
    }
    bool have_format = false;
    uint8_t chunk[8];
    while (fread(chunk, 1, sizeof(chunk), file) == sizeof(chunk)) {
        const uint32_t size = read_le32(chunk + 4);
        if (memcmp(chunk, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (size < sizeof(fmt) || fread(fmt, 1, sizeof(fmt), file) != sizeof(fmt)) {
                return false;
            }
            if (read_le16(fmt) != 1) {  // WAVE_FORMAT_PCM
                return false;
            }
            format.channels = static_cast<uint8_t>(read_le16(fmt + 2));
            format.sample_rate = read_le32(fmt + 4);
            format.bits = static_cast<uint8_t>(read_le16(fmt + 14));
            have_format = true;
            fseek(file, static_cast<long>(size - sizeof(fmt) + (size & 1)), SEEK_CUR);
        } else if (memcmp(chunk, "data", 4) == 0) {
            return have_format && (format.bits == 8 || format.bits == 16) &&
                   (format.channels == 1 || format.channels == 2) && format.sample_rate >= 8000 &&
                   format.sample_rate <= 48000;
        } else {
            fseek(file, static_cast<long>(size + (size & 1)), SEEK_CUR);
        }
    }
    return false;
}

std::string url_to_path(const std::string &url)
{
    constexpr std::string_view FILE_SCHEME = "file://";
    if (url.rfind(FILE_SCHEME, 0) == 0) {
        return url.substr(FILE_SCHEME.size());
    }
    return url;
}

class KiraPlaybackImpl final : public audio::PlaybackIface {
public:
    bool open(EventCallback callback) override
    {
        std::lock_guard lock(mutex_);
        callback_ = std::move(callback);
        opened_ = true;
        return true;
    }

    void close() override
    {
        stop();
        std::lock_guard lock(mutex_);
        callback_ = nullptr;
        opened_ = false;
    }

    bool play(const std::string &url) override
    {
        if (!stop()) return false;
        std::lock_guard lock(mutex_);
        if (!opened_) {
            return false;
        }
        pending_url_ = url;
        stop_requested_ = false;
        paused_ = false;
        running_ = true;
        // Internal-RAM stack: the task reads audio files from flash (LittleFS).
        if (xTaskCreatePinnedToCore(task_entry, "kira_play", 8192, this, 5, &task_, 0) != pdPASS) {
            running_ = false;
            return false;
        }
        return true;
    }

    bool pause() override
    {
        if (!running_) {
            return false;
        }
        paused_ = true;
        notify(audio::PlayState::Paused);
        return true;
    }

    bool resume() override
    {
        if (!running_) {
            return false;
        }
        paused_ = false;
        notify(audio::PlayState::Playing);
        return true;
    }

    bool stop() override
    {
        if (!running_) {
            return true;
        }
        stop_requested_ = true;
        paused_ = false;
        // Wait for the playback task to finish (it checks the flag every chunk).
        for (int i = 0; i < 100 && running_; ++i) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        return !running_;
    }

    bool is_opened() const override
    {
        return opened_;
    }

private:
    static void task_entry(void *arg)
    {
        auto *self = static_cast<KiraPlaybackImpl *>(arg);
        std::string url;
        {
            std::lock_guard lock(self->mutex_);
            url = self->pending_url_;
        }
        self->run(url);
        self->task_ = nullptr;
        self->notify(audio::PlayState::Idle);
        self->running_ = false;
        vTaskDelete(nullptr);
    }

    void notify(audio::PlayState state)
    {
        EventCallback callback;
        {
            std::lock_guard lock(mutex_);
            callback = callback_;
        }
        if (callback) {
            callback(state);
        }
    }

    void run(const std::string &url)
    {
        if (!kira::audio::reserve_speaker()) {
            ESP_LOGW(TAG, "speaker is busy with a voice response");
            return;
        }
        struct SpeakerLease {
            ~SpeakerLease() { kira::audio::release_speaker(); }
        } lease;
        auto codec = acquire_first_interface<audio::CodecPlayerIface>();
        if (!codec) {
            ESP_LOGE(TAG, "codec player unavailable");
            return;
        }
        if (url.rfind("tone://", 0) == 0) {
            play_tone(&*codec, static_cast<uint32_t>(std::strtoul(url.c_str() + 7, nullptr, 10)));
            return;
        }
        const std::string path = url_to_path(url);
        FILE *file = fopen(path.c_str(), "rb");
        if (file == nullptr) {
            ESP_LOGE(TAG, "cannot open %s", path.c_str());
            return;
        }
        PcmFormat format;
        const bool raw = path.size() > 4 && path.compare(path.size() - 4, 4, ".pcm") == 0;
        if (!raw && !parse_wav(file, format)) {
            ESP_LOGE(TAG, "%s: only PCM WAV and raw .pcm are supported on rev1.3", path.c_str());
            fclose(file);
            return;
        }
        if (!codec->open({format.bits, format.channels, format.sample_rate})) {
            fclose(file);
            return;
        }
        notify(audio::PlayState::Playing);
        std::vector<uint8_t> buffer(CHUNK_BYTES);
        size_t n = 0;
        while (!stop_requested_ && (n = fread(buffer.data(), 1, buffer.size(), file)) > 0) {
            while (paused_ && !stop_requested_) {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            if (!codec->write_data(buffer.data(), n)) {
                break;
            }
        }
        fclose(file);
        codec->close();
    }

    void play_tone(audio::CodecPlayerIface *codec, uint32_t hz)
    {
        constexpr uint32_t RATE = 16000;
        if (hz < 100 || hz > 4000) {
            hz = 1000;
        }
        if (codec == nullptr || !codec->open({16, 1, RATE})) {
            return;
        }
        notify(audio::PlayState::Playing);
        std::vector<int16_t> buffer(RATE / 10);  // 100 ms
        uint32_t sample = 0;
        for (int block = 0; block < 10 && !stop_requested_; ++block) {
            for (auto &value : buffer) {
                value = static_cast<int16_t>(9000.0 * std::sin(2.0 * std::numbers::pi * hz * sample++ / RATE));
            }
            codec->write_data(reinterpret_cast<const uint8_t *>(buffer.data()), buffer.size() * sizeof(int16_t));
        }
        codec->close();
    }

    std::mutex mutex_;
    EventCallback callback_;
    std::string pending_url_;
    std::atomic<bool> opened_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> stop_requested_{false};
    TaskHandle_t task_ = nullptr;
};

std::weak_ptr<KiraPlaybackImpl> s_playback;

}  // namespace

class KiraPlaybackDevice : public Device {
public:
    static constexpr const char *DEVICE_NAME = "KiraAudio";

    static KiraPlaybackDevice &get_instance()
    {
        static KiraPlaybackDevice instance;
        return instance;
    }

private:
    KiraPlaybackDevice()
        : Device(std::string(DEVICE_NAME))
    {
    }
    ~KiraPlaybackDevice() = default;

    bool probe() override
    {
        return true;
    }

    std::vector<InterfaceSpec> get_interface_specs() const override
    {
        return {{audio::PlaybackIface::NAME, PLAYBACK_INSTANCE}};
    }

    bool on_init() override
    {
        auto playback = std::make_shared<KiraPlaybackImpl>();
        s_playback = playback;
        ESP_LOGI(TAG, "publishing %s (codec player backend)", PLAYBACK_INSTANCE);
        return publish_interface(PLAYBACK_INSTANCE, playback);
    }

    void on_deinit() override
    {
        interfaces_.clear();
    }
};

BROOKESIA_PLUGIN_REGISTER_SINGLETON_WITH_SYMBOL(
    Device, KiraPlaybackDevice, KiraPlaybackDevice::DEVICE_NAME, KiraPlaybackDevice::get_instance(),
    kira_playback_device_symbol
);

}  // namespace esp_brookesia::hal

#endif  // !CONFIG_BROOKESIA_HAL_ADAPTOR_AUDIO_ENABLE_PROCESSOR_IMPL

namespace kira::audio {

bool play_test_tone()
{
#if !CONFIG_BROOKESIA_HAL_ADAPTOR_AUDIO_ENABLE_PROCESSOR_IMPL
    auto playback = esp_brookesia::hal::s_playback.lock();
    if (playback) {
        if (!playback->is_opened()) {
            playback->open(nullptr);
        }
        return playback->play("tone://1000");
    }
#endif
    return false;
}

}  // namespace kira::audio
