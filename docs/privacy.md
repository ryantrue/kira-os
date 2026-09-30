# Voice and privacy model

Kira 0.3 does not keep the microphone open while the app is idle. Tapping the
sphere is the explicit authorization to create a bounded Realtime session and
open `Audio:CodecRecorder`. The software upload gate starts closed, opens only
inside that activation flow, and closes synchronously on app pause/close, mute,
session failure or the two-minute timeout. Provider teardown may finish later,
but the encoder drops frames as soon as the gate closes.

The microphone test records three seconds into RAM and replays it locally. It
does not create a network session. The Settings mute is an explicit software
capture gate; the current ES7210 HAL does not expose a separate hardware mute.
Input level is calculated from the selected raw capture channel while capture is
active.

The rev1.3 dependency profile cannot use the current ESP-SR processor build, so
0.3 does not advertise a wake word, local AFE or local VAD. If a compatible wake
provider is added later, wake processing must remain local and must not weaken
the explicit upload gate.

API keys and Home Assistant tokens are entered through the System Super
keyboard, stored in Kira's NVS settings namespace and never printed or echoed
back. Empty secret input preserves the existing value. NVS encryption is not
claimed unless it is explicitly provisioned for a device.
