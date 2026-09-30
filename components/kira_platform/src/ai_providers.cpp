/* SPDX-License-Identifier: Apache-2.0 */
#include "kira/platform/ai_providers.hpp"

#include <array>

namespace kira::platform {
namespace {

// Model names are defaults only; the user can type any model the provider serves.
constexpr std::array<AiProvider, 5> PROVIDERS{{
    {"openai", "OpenAI", "gpt-realtime", true, false, true},
    {"anthropic", "Anthropic Claude", "claude-sonnet-4-5", true, false, false},
    {"xai", "xAI Grok", "grok-4", true, false, false},
    {"google", "Google Gemini", "gemini-2.5-flash", true, false, false},
    {"kira_brain", "Kira Brain (home server)", "auto", false, true, false},
}};

}  // namespace

std::span<const AiProvider> ai_providers()
{
    return PROVIDERS;
}

const AiProvider *find_ai_provider(std::string_view id)
{
    for (const auto &provider : PROVIDERS) {
        if (provider.id == id) {
            return &provider;
        }
    }
    return nullptr;
}

const AiProvider &default_ai_provider()
{
    return PROVIDERS.front();
}

}  // namespace kira::platform
