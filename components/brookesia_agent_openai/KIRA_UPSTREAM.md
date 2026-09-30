# Kira product override

This directory vendors `espressif/brookesia_agent_openai` 0.8.1 from the ESP
Component Registry. The registry archive SHA-256 recorded by the resolved
component lock was:

`68207267754c751e670472088f3a5b85bea0e1305ef3847ffd7e64dee70a869a`

Kira keeps this exact version local because the ESP32-P4 rev1.3 build cannot
use the upstream ESP-SR processor path. The product patch disables AFE for the
Realtime encoder, maps current Realtime API stage events to AgentManager state,
and removes transcript and outbound text content from logs. Keep credentials
runtime-provisioned and rebase this override explicitly when upgrading.
