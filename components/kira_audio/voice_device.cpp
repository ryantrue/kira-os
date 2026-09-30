/* SPDX-License-Identifier: Apache-2.0 */
#include "kira/audio.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>
#include "esp_log.h"
#include "esp_opus_enc.h"
#include "esp_opus_dec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "brookesia/hal_interface.hpp"
#include "brookesia/hal_interface/device.hpp"
#include "brookesia/hal_interface/interfaces/audio/codec_recorder.hpp"
#include "brookesia/hal_interface/interfaces/audio/codec_player.hpp"
#include "brookesia/hal_interface/interfaces/audio/processor.hpp"

namespace kira::audio {
namespace {
namespace hal = esp_brookesia::hal;
constexpr const char *TAG = "kira_voice_audio";
std::atomic<bool> authorized{false}, muted{false}, microphone_busy{false}, speaker_busy{false};
std::atomic<float> input_level{0}, input_gain{0};
std::atomic<bool> gain_set{false};
std::atomic<uint64_t> response_frames{0};
std::mutex test_mutex;
std::string test_state = "Not tested";

void set_test_state(const char *text)
{
    std::lock_guard lock(test_mutex);
    test_state = text;
}

// The board descriptor exposes four capture slots (FL, RE, FR, FC), while the
// Realtime adapter requires mono. Select the first declared slot consistently;
// physical acceptance must verify that this is the intended microphone route.
void extract_mono(const std::vector<int16_t> &raw, size_t channels, std::vector<int16_t> &mono)
{
    uint64_t squares = 0;
    for (size_t i = 0; i < mono.size(); ++i) {
        mono[i] = raw[i * channels];
        const int32_t value = mono[i];
        squares += static_cast<int64_t>(value) * value;
    }
    input_level = static_cast<float>(std::sqrt(static_cast<double>(squares) / mono.size()) / 32768.0);
}

class RawOpusEncoder final : public hal::audio::EncoderIface {
public:
    ~RawOpusEncoder() override { stop(); }
    std::vector<std::string> get_afe_wake_words() override { return {}; }
    bool start(const hal::audio::EncoderDynamicConfig &config, Callbacks callbacks) override
    {
        std::lock_guard lock(mutex_);
        if (started_) return true;
        if (!authorized || config.enable_afe || config.type != hal::audio::CodecFormat::OPUS ||
            config.general.channels != 1 || config.general.sample_bits != 16 ||
            config.general.sample_rate != 16000 || config.general.frame_duration != 60) {
            ESP_LOGE(TAG, "capture config unsupported: raw mono Opus 16kHz/60ms, no AFE required");
            return false;
        }
        if (microphone_busy.exchange(true)) return false;
        microphone_owned_ = true;
        recorder_ = hal::acquire_first_interface<hal::audio::CodecRecorderIface>();
        if (!recorder_ || recorder_->get_info().bits != 16 || recorder_->get_info().channels == 0 ||
            recorder_->get_info().sample_rate != 16000 || !recorder_->open()) {
            cleanup();
            return false;
        }
        auto opus = ESP_OPUS_ENC_CONFIG_DEFAULT();
        opus.sample_rate = 16000;
        opus.channel = 1;
        opus.bitrate = 24000;
        opus.frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS;
        if (esp_opus_enc_open(&opus, sizeof(opus), &encoder_) != ESP_AUDIO_ERR_OK) {
            cleanup();
            return false;
        }
        int in_bytes = 0, out_bytes = 0;
        if (esp_opus_enc_get_frame_size(encoder_, &in_bytes, &out_bytes) != ESP_AUDIO_ERR_OK ||
            in_bytes != 1920 || out_bytes > 2048) {
            cleanup();
            return false;
        }
        mono_.resize(in_bytes / sizeof(int16_t));
        raw_.resize(mono_.size() * recorder_->get_info().channels);
        callbacks_ = std::move(callbacks);
        if (gain_set) recorder_->set_general_gain(input_gain);
        else input_gain = recorder_->get_info().general_gain;
        started_ = true;
        paused_ = false;
        frames_ = 0;
        ESP_LOGI(TAG, "capture opened: CodecRecorder -> channel 0 -> mono Opus (local AFE unavailable)");
        return true;
    }
    int read_encoded_data(uint8_t *data, size_t size) override
    {
        std::lock_guard lock(mutex_);
        if (!started_ || !authorized || paused_) return 0;
        if (!recorder_->read_data(reinterpret_cast<uint8_t *>(raw_.data()), raw_.size() * sizeof(int16_t))) {
            ESP_LOGE(TAG, "capture read failed");
            return -1;
        }
        extract_mono(raw_, recorder_->get_info().channels, mono_);
        // Check again after the blocking read: closing the app revokes upload
        // immediately, even while AgentManager is still stopping its worker.
        if (!authorized || muted) return 0;
        if (callbacks_.recorder_data) {
            callbacks_.recorder_data(reinterpret_cast<uint8_t *>(mono_.data()), mono_.size() * sizeof(int16_t));
        }
        esp_audio_enc_in_frame_t input{reinterpret_cast<uint8_t *>(mono_.data()), static_cast<uint32_t>(mono_.size() * sizeof(int16_t))};
        esp_audio_enc_out_frame_t output{data, static_cast<uint32_t>(size), 0};
        if (esp_opus_enc_process(encoder_, &input, &output) != ESP_AUDIO_ERR_OK) return -1;
        if (++frames_ == 1) ESP_LOGI(TAG, "capture first frame encoded; uplink authorized");
        return static_cast<int>(output.encoded_bytes);
    }
    void stop() override { std::lock_guard lock(mutex_); cleanup(); }
    void pause() override { paused_ = true; }
    void resume() override { paused_ = false; }
    bool is_started() const override { return started_; }
    bool is_paused() const override { return paused_; }
private:
    void cleanup()
    {
        if (encoder_) esp_opus_enc_close(encoder_);
        encoder_ = nullptr;
        if (recorder_) recorder_->close();
        recorder_.reset();
        callbacks_ = {};
        raw_.clear(); mono_.clear();
        started_ = false;
        input_level = 0;
        if (microphone_owned_) microphone_busy = false;
        microphone_owned_ = false;
    }
    std::mutex mutex_;
    hal::InterfaceHandle<hal::audio::CodecRecorderIface> recorder_;
    void *encoder_ = nullptr;
    Callbacks callbacks_;
    std::vector<int16_t> raw_, mono_;
    std::atomic<bool> started_{false}, paused_{false};
    uint32_t frames_ = 0;
    bool microphone_owned_ = false;
};

class RawOpusDecoder final : public hal::audio::DecoderIface {
public:
    ~RawOpusDecoder() override { stop(); }
    bool start(const hal::audio::DecoderDynamicConfig &config) override
    {
        std::lock_guard lock(mutex_);
        if (started_) return true;
        if (!authorized || config.type != hal::audio::CodecFormat::OPUS || config.general.channels != 1 ||
            config.general.sample_bits != 16 || config.general.sample_rate != 16000 || !reserve_speaker()) return false;
        speaker_owned_ = true;
        player_ = hal::acquire_first_interface<hal::audio::CodecPlayerIface>();
        auto opus = ESP_OPUS_DEC_CONFIG_DEFAULT();
        opus.sample_rate = 16000;
        opus.channel = 1;
        opus.frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS;
        opus.self_delimited = false;
        if (!player_ || esp_opus_dec_open(&opus, sizeof(opus), &decoder_) != ESP_AUDIO_ERR_OK ||
            !player_->open({16, 1, 16000})) {
            cleanup();
            return false;
        }
        pcm_.resize(16000 * 120 / 1000); // Maximum Opus packet duration, 120ms.
        started_ = true;
        first_frame_ = true;
        ESP_LOGI(TAG, "response decoder opened: Opus -> CodecPlayer");
        return true;
    }
    bool feed_data(const uint8_t *data, size_t size) override
    {
        std::lock_guard lock(mutex_);
        if (!started_) return false;
        if (!authorized) return true; // Drop in-flight downlink after app close.
        esp_audio_dec_in_raw_t input{};
        input.buffer = const_cast<uint8_t *>(data);
        input.len = size;
        esp_audio_dec_out_frame_t output{};
        output.buffer = reinterpret_cast<uint8_t *>(pcm_.data());
        output.len = pcm_.size() * sizeof(int16_t);
        esp_audio_dec_info_t info{};
        if (esp_opus_dec_decode(decoder_, &input, &output, &info) != ESP_AUDIO_ERR_OK || input.consumed != size) {
            ESP_LOGE(TAG, "response Opus decode failed");
            return false;
        }
        if (!authorized) return true;
        if (!player_->write_data(output.buffer, output.decoded_size)) return false;
        ++response_frames;
        if (first_frame_) {
            ESP_LOGI(TAG, "response first PCM frame played through CodecPlayer");
            first_frame_ = false;
        }
        return true;
    }
    void stop() override { std::lock_guard lock(mutex_); cleanup(); }
    bool is_started() const override { return started_; }
private:
    void cleanup()
    {
        if (decoder_) esp_opus_dec_close(decoder_);
        decoder_ = nullptr;
        if (player_) player_->close();
        player_.reset(); pcm_.clear(); started_ = false;
        if (speaker_owned_) release_speaker();
        speaker_owned_ = false;
    }
    std::mutex mutex_;
    hal::InterfaceHandle<hal::audio::CodecPlayerIface> player_;
    void *decoder_ = nullptr;
    std::vector<int16_t> pcm_;
    std::atomic<bool> started_{false};
    bool speaker_owned_ = false, first_frame_ = true;
};

void microphone_test_task(void *)
{
    bool success = false;
    {
        auto recorder = hal::acquire_first_interface<hal::audio::CodecRecorderIface>();
        if (recorder && recorder->get_info().bits == 16 && recorder->get_info().channels > 0 &&
            recorder->get_info().sample_rate == 16000 && recorder->open()) {
            if (gain_set) recorder->set_general_gain(input_gain);
            else input_gain = recorder->get_info().general_gain;
            set_test_state("Recording locally (3 seconds)");
            std::vector<int16_t> raw(320 * recorder->get_info().channels), mono(320), recording;
            recording.reserve(48000);
            success = true;
            for (int i = 0; i < 150; ++i) {
                if (muted || !recorder->read_data(reinterpret_cast<uint8_t *>(raw.data()), raw.size() * 2)) {
                    success = false; break;
                }
                extract_mono(raw, recorder->get_info().channels, mono);
                recording.insert(recording.end(), mono.begin(), mono.end());
            }
            recorder->close();
            if (success && reserve_speaker()) {
                auto player = hal::acquire_first_interface<hal::audio::CodecPlayerIface>();
                success = player && player->open({16, 1, 16000});
                if (success) {
                    set_test_state("Replaying local recording");
                    for (size_t offset = 0; offset < recording.size() && success; offset += 320) {
                        success = player->write_data(reinterpret_cast<uint8_t *>(recording.data() + offset), 640);
                    }
                    if (player) player->close();
                }
                release_speaker();
            } else success = false;
        }
    }
    set_test_state(success ? "Local recording/replay complete" : "Microphone test failed or busy");
    ESP_LOGI(TAG, "local microphone test %s (no network audio)", success ? "completed" : "failed");
    input_level = 0;
    microphone_busy = false;
    vTaskDelete(nullptr);
}
} // namespace

MicrophoneStatus microphone_status()
{
    MicrophoneStatus status;
    auto recorder = hal::acquire_first_interface<hal::audio::CodecRecorderIface>();
    status.available = static_cast<bool>(recorder);
    status.running = microphone_busy;
    status.muted = muted;
    status.level = input_level;
    if (recorder) {
        status.sample_rate = recorder->get_info().sample_rate;
        status.channels = recorder->get_info().channels;
        status.gain = gain_set ? input_gain.load() : recorder->get_info().general_gain;
    }
    std::lock_guard lock(test_mutex);
    status.test_state = test_state;
    return status;
}
bool start_microphone_test()
{
    if (authorized || muted || microphone_busy.exchange(true)) return false;
    if (xTaskCreate(microphone_test_task, "kira_mic_test", 8192, nullptr, 5, nullptr) != pdPASS) {
        microphone_busy = false; return false;
    }
    return true;
}
bool set_microphone_gain(float gain_db)
{
    if (!std::isfinite(gain_db) || gain_db < 0 || gain_db > 37.5F || microphone_busy.exchange(true)) return false;
    auto recorder = hal::acquire_first_interface<hal::audio::CodecRecorderIface>();
    bool result = recorder && recorder->open();
    if (result) {
        result = recorder->set_general_gain(gain_db);
        recorder->close();
    }
    if (result) { input_gain = gain_db; gain_set = true; }
    microphone_busy = false;
    return result;
}
void set_microphone_muted(bool value) { muted = value; }
void set_session_authorized(bool value) { authorized = value; }
bool session_authorized() { return authorized; }
uint64_t response_frame_count() { return response_frames; }
bool reserve_speaker() { return !speaker_busy.exchange(true); }
void release_speaker() { speaker_busy = false; }
} // namespace kira::audio

#if !CONFIG_BROOKESIA_HAL_ADAPTOR_AUDIO_ENABLE_PROCESSOR_IMPL
namespace esp_brookesia::hal {
class KiraVoiceDevice final : public Device {
public:
    static KiraVoiceDevice &get_instance() { static KiraVoiceDevice value; return value; }
private:
    KiraVoiceDevice() : Device("KiraVoice") {}
    bool probe() override { return true; }
    std::vector<InterfaceSpec> get_interface_specs() const override
    {
        return {{audio::EncoderIface::NAME, "Audio:Encoder:0"}, {audio::DecoderIface::NAME, "Audio:Decoder:0"}};
    }
    bool on_init() override
    {
        return publish_interface("Audio:Encoder:0", std::make_shared<kira::audio::RawOpusEncoder>()) &&
               publish_interface("Audio:Decoder:0", std::make_shared<kira::audio::RawOpusDecoder>());
    }
    void on_deinit() override { interfaces_.clear(); }
};
BROOKESIA_PLUGIN_REGISTER_SINGLETON_WITH_SYMBOL(
    Device, KiraVoiceDevice, "KiraVoice", KiraVoiceDevice::get_instance(), kira_voice_device_symbol
);
} // namespace esp_brookesia::hal
#endif
