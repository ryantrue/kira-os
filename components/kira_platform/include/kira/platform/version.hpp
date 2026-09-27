/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <string>

namespace kira::platform {

// Single source of the Kira OS version: version.txt -> PROJECT_VER (root
// CMakeLists.txt) -> esp_app_desc_t. Format: "MAJOR.MINOR.PATCH+commit".
[[nodiscard]] std::string firmware_version();       // full, e.g. "0.2.0+5baa2d6"
[[nodiscard]] std::string firmware_semver();        // without build metadata, e.g. "0.2.0"
[[nodiscard]] std::string firmware_build_date();    // compile date and time

}  // namespace kira::platform
