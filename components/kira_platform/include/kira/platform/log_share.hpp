/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <string>

#include "esp_err.h"

namespace kira::platform::log_share {

// Temporary log download over Wi-Fi.
//
// start() opens a WPA2 access point "Kira-XXXX" (password shown in Kira
// Center) next to the normal Wi-Fi connection and serves:
//   http://192.168.4.1/          small page with links
//   http://192.168.4.1/log       download kira-log.txt, then delete it on the device
//   http://192.168.4.1/log?keep=1  download without deleting
// The same pages are reachable on the home network address while sharing is on.
// Sharing stops by itself after AUTO_STOP_MINUTES.
inline constexpr int AUTO_STOP_MINUTES = 15;

struct Status {
    bool running = false;
    std::string ssid;
    std::string password;
    std::string ap_url;    // http://192.168.4.1/log
    std::string lan_url;   // http://<home network address>/log, empty when not connected
    std::string message;
};

esp_err_t start();
void stop();
[[nodiscard]] Status status();

}  // namespace kira::platform::log_share
