/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

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

}  // namespace kira::audio
