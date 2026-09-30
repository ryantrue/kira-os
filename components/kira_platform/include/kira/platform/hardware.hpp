/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <string>
#include <vector>

namespace kira::platform::hardware {

struct Pin {
    int number = -1;
    std::string function;
    std::string direction;
    std::string state;
    bool reserved = true;
    bool controllable = false;
};

// Reads hardware configuration without reconfiguring pads. Board/host pins are
// System/Reserved; unspecified pads remain read-only until wiring is qualified.
[[nodiscard]] std::vector<Pin> gpio_status();

struct Connectivity {
    bool wifi_connected = false;
    std::string wifi_message;
    bool bluetooth_control = false;
    std::string bluetooth_message;
};

[[nodiscard]] Connectivity connectivity_status();

}  // namespace kira::platform::hardware
