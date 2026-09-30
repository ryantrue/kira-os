/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <cstdint>
#include <string>

namespace kira::audio {

// Kira's audio playback provider for ESP32-P4 rev1.3.
//
// Brookesia's AudioPlayback service needs the HAL interface "Audio:Playback".
// In brookesia_hal_adaptor that interface is only published by the audio
// processor implementation, which is disabled on rev1.3 (it depends on ESP-SR,
// available for rev3 silicon only). Without it the service cannot start and
// the speaker, volume and mute controls stay unavailable.
//
// kira_audio publishes "Audio:Playback" itself on top of the codec player
// (ES8311 + PA), which works on rev1.3. Supported sources:
//   file:///littlefs/...wav  or  /sdcard/...wav   PCM WAV, 8/16-bit, mono/stereo
//   .pcm files                                     raw 16-bit mono 16 kHz
//   tone://<hz>                                    one second test tone
// Compressed formats (MP3/AAC/Opus) need the processor path and are rejected.

// Plays a short test tone through the speaker. Returns false when the codec
// is unavailable. Safe to call from any task.
bool play_test_tone();

struct MicrophoneStatus {
    bool available = false;
    bool running = false;
    bool muted = false;  // Software capture gate, not an ES7210 hardware switch.
    float level = 0;
    float gain = 0;
    uint32_t sample_rate = 0;
    uint8_t channels = 0;
    std::string test_state;
};

MicrophoneStatus microphone_status();
bool start_microphone_test();  // Three seconds capture + local replay; never uploads.
bool set_microphone_gain(float gain_db);
void set_microphone_muted(bool muted);

// Closed at boot and synchronously closed on app pause/stop. Only explicit
// activation opens this gate, independently of asynchronous agent teardown.
void set_session_authorized(bool authorized);
bool session_authorized();
uint64_t response_frame_count();

// Shared codec lease prevents test tones and streamed response playback racing.
bool reserve_speaker();
void release_speaker();

}  // namespace kira::audio
