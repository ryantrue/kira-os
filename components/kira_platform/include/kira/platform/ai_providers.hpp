/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <span>
#include <string_view>

namespace kira::platform {

// Static description of an AI backend Kira can be configured to use.
// Adding a provider means adding one entry to the table in ai_providers.cpp;
// the settings UI is generated from this table.
struct AiProvider {
    std::string_view id;             // persistent identifier stored in settings
    std::string_view display_name;   // shown in the UI
    std::string_view default_model;  // pre-filled model name, user-editable
    bool needs_api_key;              // cloud providers need a key
    bool needs_endpoint;             // self-hosted providers need a URL
    bool voice_realtime;             // can serve the realtime voice session directly
};

[[nodiscard]] std::span<const AiProvider> ai_providers();
[[nodiscard]] const AiProvider *find_ai_provider(std::string_view id);
[[nodiscard]] const AiProvider &default_ai_provider();

}  // namespace kira::platform
