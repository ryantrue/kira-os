/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <cstdint>
#include <string>
#include "sdkconfig.h"
#include "brookesia/service_helper/media/display.hpp"
#include "brookesia/service_manager/service/manager.hpp"

class Display final {
public:
    using GestureData = esp_brookesia::service::helper::Display::TouchGestureConfig;
    struct Config {
        int lvgl_task_core_id = CONFIG_BROOKESIA_HAL_ADAPTOR_DISPLAY_LCD_PANEL_INIT_THREAD_CORE_ID;
        GestureData gesture_data{};
    };

    static Display &get_instance();
    bool start(const Config &config);
    [[nodiscard]] uint32_t width() const { return width_; }
    [[nodiscard]] uint32_t height() const { return height_; }

private:
    Display() = default;
    bool start_service();
    bool start_lvgl(int core_id);
    bool activate_lvgl();
    bool start_gestures();

    esp_brookesia::service::ServiceBinding binding_;
    std::string output_name_;
    uint32_t output_id_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool has_backlight_ = false;
    GestureData gesture_data_{};
};

