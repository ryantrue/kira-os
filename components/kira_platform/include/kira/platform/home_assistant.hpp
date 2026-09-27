/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kira::platform::home_assistant {

// Minimal Home Assistant REST client (long-lived access token).
// Lists toggleable entities (light, switch, input_boolean, fan) and toggles them.
struct Entity {
    std::string entity_id;
    std::string name;
    std::string state;  // "on", "off", "unavailable", ...
};

struct Status {
    bool configured = false;  // URL and token present
    bool busy = false;
    bool connected = false;   // last request succeeded
    std::string message;
    std::vector<Entity> entities;
    uint32_t generation = 0;
};

inline constexpr size_t MAX_ENTITIES = 64;

[[nodiscard]] Status status();
void test_async();
void refresh_async();
void toggle_async(const std::string &entity_id);

}  // namespace kira::platform::home_assistant
