# Voice and privacy model

The device continuously captures microphone frames for local AFE, VAD, noise
energy and WakeNet processing. This is not equivalent to continuously streaming
audio: before `Hey, Kira`, frames remain on-device and expire from bounded
buffers.

Cloud transport can open only after a verified wake transition. It closes on
conversation end, explicit mute, network loss or timeout. The UI always exposes
microphone and transport state. A hardware or settings mute disables capture,
not merely wake-word callbacks.

Credentials are entered through device provisioning and stored in encrypted
NVS. CI, source files, build logs and release artifacts must contain no API key.

