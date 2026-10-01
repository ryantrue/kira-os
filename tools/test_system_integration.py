#!/usr/bin/env python3
"""Source contracts for the Kira 0.4 native System Super integration."""

from __future__ import annotations

import json
import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[1]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"system integration contract failed: {message}")


def read(relative: str) -> str:
    return (ROOT / relative).read_text()


def walk_json(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from walk_json(child)
    elif isinstance(value, list):
        for child in value:
            yield from walk_json(child)


def test_settings_screen() -> None:
    screen = json.loads(read("components/brookesia_app_settings/package/res/screens/kira.json"))
    nodes = list(walk_json(screen))
    ids = [node["id"] for node in nodes if isinstance(node.get("id"), str)]
    require(len(ids) == len(set(ids)), "Kira Settings screen contains duplicate object ids")

    required_ids = {
        "assistant_section", "open", "autostart", "idle",
        "ai_section", "provider", "model", "key",
        "update_section", "version", "update_status", "update_check", "update_install",
    }
    require(required_ids <= set(ids), f"Kira Settings screen is missing ids: {sorted(required_ids - set(ids))}")

    forbidden_ids = {
        "microphone_section", "mic_status", "mic_mute", "mic_gain_down", "mic_gain_up", "mic_test", "tone",
        "ha_section", "ha_status", "ha_url", "ha_token", "ha_test", "ha_refresh", "ha_page", "ha_prev", "ha_next",
        "storage_section", "sd_status", "sd_refresh", "sd_format",
        "logs_section", "log_enable", "log_status", "log_share", "log_clear",
        "connectivity_section", "connectivity_status", "hardware_section", "gpio_status",
    } | {f"entity{i}" for i in range(8)}
    require(not (forbidden_ids & set(ids)),
            f"Kira Settings still owns system/app surfaces: {sorted(forbidden_ids & set(ids))}")

    actions = {
        event["action"]
        for node in nodes
        for event in node.get("events", [])
        if isinstance(event, dict) and isinstance(event.get("action"), str)
    }
    required_actions = {
        "settings.kira.open", "settings.kira.autostart", "settings.kira.idle",
        "settings.kira.provider", "settings.kira.model", "settings.kira.key",
        "settings.kira.update_check", "settings.kira.update_install",
    }
    require(required_actions <= actions, f"Kira Settings screen is missing actions: {sorted(required_actions - actions)}")
    require(not any("keyboard" in str(node.get("type", "")).lower() for node in nodes),
            "Kira Settings embeds a second keyboard")

    sound = json.loads(read("components/brookesia_app_settings/package/res/screens/sound.json"))
    sound_nodes = list(walk_json(sound))
    sound_ids = {node["id"] for node in sound_nodes if isinstance(node.get("id"), str)}
    require({"mic_status", "mic_mute", "mic_gain_down", "mic_gain_up", "mic_test", "speaker_test"} <= sound_ids,
            "native Sound page is missing microphone/speaker controls")
    sound_actions = {
        event["action"]
        for node in sound_nodes
        for event in node.get("events", [])
        if isinstance(event, dict) and isinstance(event.get("action"), str)
    }
    require({
        "settings.sound.mic_mute", "settings.sound.mic_gain_down", "settings.sound.mic_gain_up",
        "settings.sound.mic_test", "settings.sound.speaker_test",
    } <= sound_actions, "native Sound page audio actions are incomplete")

    home_assistant_settings = json.loads(read("components/brookesia_app_settings/package/res/screens/home_assistant.json"))
    ha_settings_nodes = list(walk_json(home_assistant_settings))
    ha_settings_ids = {node["id"] for node in ha_settings_nodes if isinstance(node.get("id"), str)}
    require({"status", "url", "token", "test"} <= ha_settings_ids,
            "Home Assistant settings page is missing connection controls")
    require("components/kira_ha_app/app.cpp" and "kira.home_assistant" in read("components/kira_ha_app/app.cpp"),
            "standalone Home Assistant native app is missing")
    require("kira::home_assistant_app::ensure_linked();" in read("main/main.cpp"),
            "standalone Home Assistant app provider is not linked")

    storage = json.loads(read("components/brookesia_app_settings/package/res/screens/storage.json"))
    storage_nodes = list(walk_json(storage))
    storage_ids = {node["id"] for node in storage_nodes if isinstance(node.get("id"), str)}
    require({"sd_status", "sd_refresh", "sd_format"} <= storage_ids,
            "native Storage page is missing SD controls")
    storage_actions = {
        event["action"]
        for node in storage_nodes
        for event in node.get("events", [])
        if isinstance(event, dict) and isinstance(event.get("action"), str)
    }
    require({"settings.storage.sd_refresh", "settings.storage.sd_format"} <= storage_actions,
            "native Storage page SD actions are incomplete")

    template = json.loads(read("components/brookesia_app_settings/package/res/templates/setting_row.json"))
    bindings = template.get("node", {}).get("bindings", {})
    require(bindings.get("commonProps.disabled") == "commonProps.disabled", "setting rows do not bind disabled state")
    require(bindings.get("commonProps.hidden") == "commonProps.hidden", "setting rows do not bind hidden state")

def test_settings_controller() -> None:
    bridge = read("components/kira_settings/settings_bridge.cpp")
    lifecycle = read("components/brookesia_app_settings/src/app/lifecycle.ipp")
    cmake = read("components/brookesia_app_settings/CMakeLists.txt")
    for call in (".start(context)", ".stop(context)", ".set_active(context", ".action(context", ".poll(context)"):
        require(f"Bridge::instance(){call}" in lifecycle, f"stock Settings lifecycle is missing Bridge{call}")
    require('set(COMPONENT_REQUIRES "kira_settings")' in cmake, "stock Settings does not link kira_settings")
    require('current_page_ == PAGE_SOUND' in lifecycle, "native Sound page does not activate the audio bridge")
    require('action.starts_with("settings.sound.mic_")' in lifecycle, "native Sound microphone actions are not routed")
    require('current_page_ == PAGE_STORAGE' in lifecycle, "native Storage page does not activate the storage bridge")
    require('action.starts_with("settings.storage.")' in lifecycle, "native Storage actions are not routed")
    require('"settings.storage.sd_refresh"' in bridge and '"settings.storage.sd_format"' in bridge,
            "native Storage SD actions have no controller branches")
    require("show_keyboard(make_options(field)" in bridge, "Kira fields do not use the system keyboard")
    require("if (!result.text.empty()) err = settings.set_ai_key" in bridge, "empty input can erase the AI key")
    require("if (!result.text.empty()) err = settings.set_ha_token" in bridge, "empty input can erase the HA token")
    require("Internal LittleFS is not formatted" in bridge, "SD destructive confirmation lacks the LittleFS boundary")

    handled = set(re.findall(r'action\s*==\s*"(settings\.kira\.[a-z0-9_]+)"', bridge))
    screen = json.loads(read("components/brookesia_app_settings/package/res/screens/kira.json"))
    actions = {
        event["action"]
        for node in walk_json(screen)
        for event in node.get("events", [])
        if isinstance(event, dict) and isinstance(event.get("action"), str)
    }
    static_actions = {action for action in actions if not re.fullmatch(r"settings\.kira\.entity[0-7]", action)}
    require(static_actions <= handled, f"Settings actions have no controller branch: {sorted(static_actions - handled)}")


def test_voice_pipeline() -> None:
    app = read("components/kira_app/app.cpp")
    session = read("components/kira_app/voice_session.cpp")
    audio = read("components/kira_audio/voice_device.cpp")
    app_cmake = read("components/kira_app/CMakeLists.txt")
    audio_cmake = read("components/kira_audio/CMakeLists.txt")
    main_manifest = read("main/idf_component.yml")
    agent = read("components/brookesia_agent_openai/src/agent_openai.cpp")
    agent_header = read("components/brookesia_agent_openai/include/brookesia/agent_openai/agent_openai.hpp")
    realtime = read("components/brookesia_agent_openai/openai/https_client.c")

    require("surface.set_tap_handler([] { kira::voice::activate(); })" in app, "sphere tap is not connected to voice activation")
    require(app.count("kira::voice::stop();") >= 2, "pause and stop do not synchronously close the voice privacy gate")
    require('"voice_session.cpp"' in app_cmake, "voice session is not built")
    for token in ("SetTargetAgent", "SetChatMode", 'action("Activate")', 'action("Start")', 'action("Stop")'):
        require(token in session, f"voice session is missing AgentManager stage {token}")
    require("set_session_authorized(false)" in session, "voice session never closes the upload gate")
    require("microphone_status().muted" in session, "a closed microphone gate does not block voice activation")
    require("pdMS_TO_TICKS(120000)" in session, "voice session has no bounded maximum duration")

    for token in ("CodecRecorderIface", "esp_opus_enc_config_t", "CodecPlayerIface",
                  "esp_opus_dec_cfg_t", '"Audio:Encoder:0"', '"Audio:Decoder:0"'):
        require(token in audio, f"raw voice device is missing {token}")
    require("!authorized || config.enable_afe" in audio, "encoder is not gated or still accepts unavailable AFE")
    require('"voice_device.cpp"' in audio_cmake, "voice device is not built")

    require("override_path: ../components/brookesia_agent_openai" in main_manifest,
            "root manifest does not pin the Kira OpenAI product override")
    require("bool configure(const OpenaiInfo &info)" in agent_header, "runtime OpenAI configuration API is missing")
    require("set_listening(true)" in agent and "set_speaking(true)" in agent,
            "Realtime events are not mapped to AgentManager state")
    require('"https://api.openai.com/v1/realtime/calls"' in realtime, "Realtime GA calls endpoint is missing")
    require('name=\\"sdp\\"' in realtime and 'name=\\"session\\"' in realtime,
            "Realtime call does not send the required multipart fields")
    require(".disable_auto_redirect = true" in realtime, "credentialed Realtime request follows redirects")

    # Do not regress to active logs of credentials, full provider payloads or transcripts.
    sensitive = re.compile(r"(?:BROOKESIA|ESP)_LOG[A-Z]*\([^\n;]*(?:api_key|transcript|payload|Peer data handler|Sending text:)", re.I)
    for relative in (
        "components/brookesia_agent_openai/src/agent_openai.cpp",
        "components/brookesia_agent_openai/openai/openai.c",
        "components/brookesia_agent_openai/openai/openai_datachannel.cpp",
    ):
        active = "\n".join(line for line in read(relative).splitlines() if not line.lstrip().startswith("//"))
        require(not sensitive.search(active), f"sensitive provider content may be logged in {relative}")


def test_pinned_overrides() -> None:
    settings_manifest = read("main/idf_component.yml")
    require(re.search(r'override_path:\s*["\']?\.\./components/brookesia_app_settings["\']?', settings_manifest),
            "stock Settings product override is not pinned")
    provenance = read("components/brookesia_agent_openai/KIRA_UPSTREAM.md")
    require("68207267754c751e670472088f3a5b85bea0e1305ef3847ffd7e64dee70a869a" in provenance,
            "OpenAI product override has no exact upstream archive provenance")
    packager = read("scripts/package-candidate.py")
    require('OUT / "INSTALL.md"' in packager, "candidate artifact does not include its installation guide")


def main() -> None:
    test_settings_screen()
    test_settings_controller()
    test_voice_pipeline()
    test_pinned_overrides()
    print("Kira 0.4 system integration contracts passed")


if __name__ == "__main__":
    main()
