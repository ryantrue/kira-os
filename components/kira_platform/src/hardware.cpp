/* SPDX-License-Identifier: Apache-2.0 */
#include "kira/platform/hardware.hpp"

#include <algorithm>
#include <iterator>
#include <mutex>

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "kira/platform/worker.hpp"
#include "sdkconfig.h"
#include "kira_board_metadata.hpp"

namespace kira::platform::hardware {

std::vector<Pin> gpio_status()
{
    std::vector<Pin> result;
    for (int number = 0; number < GPIO_NUM_MAX; ++number) {
        if (!GPIO_IS_VALID_GPIO(number)) {
            continue;
        }
        Pin pin;
        pin.number = number;
        const auto entry = std::find_if(std::begin(board_metadata::pins), std::end(board_metadata::pins),
            [number](const auto &item) { return item.number == number; });
        pin.function = entry == std::end(board_metadata::pins) ? "Wiring unverified / read-only" : entry->function;
        gpio_io_config_t config = {};
        if (gpio_get_io_config(static_cast<gpio_num_t>(number), &config) == ESP_OK) {
            pin.direction = config.oe_ctrl_by_periph ? "Peripheral" :
                config.ie && config.oe ? "Input/output" : config.ie ? "Input" : config.oe ? "Output" : "Disabled";
            // gpio_get_level returns a false zero for output-only pads. Never
            // enable input just to inspect a pad owned by a system peripheral.
            pin.state = config.ie ? (gpio_get_level(static_cast<gpio_num_t>(number)) ? "High (sample)" : "Low (sample)")
                                  : "Input sensing disabled";
        } else {
            pin.direction = "Unavailable";
            pin.state = "Unavailable";
        }
        result.push_back(std::move(pin));
    }
    return result;
}

Connectivity connectivity_status()
{
    static std::mutex mutex;
    static Connectivity cached{false, "Checking Wi-Fi...", false,
        "Bluetooth control unavailable in this firmware"};
    static bool pending = false;
    static int64_t checked_at = 0;
    std::lock_guard lock(mutex);
    const int64_t now = esp_timer_get_time();
    if (!pending && (checked_at == 0 || now - checked_at >= 5'000'000)) {
        pending = true;
        // ESP-Hosted calls may wait for the C6. Keep this request off LVGL.
        if (!submit_job("connectivity", [] {
                wifi_ap_record_t ap = {};
                const bool connected = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
                std::lock_guard update_lock(mutex);
                cached.wifi_connected = connected;
                cached.wifi_message = connected ? "Wi-Fi connected via ESP-Hosted" :
                    "Wi-Fi disconnected; use system Wi-Fi settings";
                checked_at = esp_timer_get_time();
                pending = false;
            })) {
            pending = false;
            cached.wifi_message = "Wi-Fi status unavailable; worker busy";
        }
    }
    // No Bluetooth host/service is initialized by this product configuration.
    // C6 silicon capability does not establish a working BLE session.
    return cached;
}

}  // namespace kira::platform::hardware
