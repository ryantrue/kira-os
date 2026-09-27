/* SPDX-License-Identifier: Apache-2.0 */
#include "kira/platform/version.hpp"

#include "esp_app_desc.h"

namespace kira::platform {

std::string firmware_version()
{
    return esp_app_get_description()->version;
}

std::string firmware_semver()
{
    std::string version = firmware_version();
    const size_t plus = version.find('+');
    if (plus != std::string::npos) {
        version.resize(plus);
    }
    return version;
}

std::string firmware_build_date()
{
    const esp_app_desc_t *desc = esp_app_get_description();
    return std::string(desc->date) + " " + desc->time;
}

}  // namespace kira::platform
