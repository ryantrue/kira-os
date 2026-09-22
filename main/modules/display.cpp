/* SPDX-License-Identifier: Apache-2.0 */
#include <vector>
#include "boost/json/array.hpp"
#include "boost/json/value.hpp"
#include "brookesia/gui_lvgl.hpp"
#include "brookesia/lib_utils/describe_helpers.hpp"
#include "private/utils.hpp"
#include "modules/display.hpp"

using namespace esp_brookesia;
using DisplayHelper = service::helper::Display;

namespace { constexpr uint32_t TIMEOUT_MS = 1000; }

Display &Display::get_instance()
{
    static Display display;
    return display;
}

bool Display::start(const Config &config)
{
    gesture_data_ = config.gesture_data;
    BROOKESIA_CHECK_FALSE_RETURN(start_service(), false, "Display service failed");
    BROOKESIA_CHECK_FALSE_RETURN(start_lvgl(config.lvgl_task_core_id), false, "LVGL source failed");
    BROOKESIA_CHECK_FALSE_RETURN(activate_lvgl(), false, "LVGL activation failed");
    BROOKESIA_CHECK_FALSE_RETURN(start_gestures(), false, "Touch gestures failed");
    if (has_backlight_) {
        BROOKESIA_CHECK_FALSE_RETURN(
            DisplayHelper::call_function_async(
                DisplayHelper::FunctionId::SetBacklightOnOff, output_id_, true
            ), false, "Backlight enable failed"
        );
    }
    return true;
}

bool Display::start_service()
{
    BROOKESIA_CHECK_FALSE_RETURN(DisplayHelper::is_available(), false, "Display unavailable");
    binding_ = service::ServiceManager::get_instance().bind(DisplayHelper::get_name().data());
    BROOKESIA_CHECK_FALSE_RETURN(binding_.is_valid(), false, "Display bind failed");
    auto result = DisplayHelper::call_function_sync<boost::json::array>(
        DisplayHelper::FunctionId::GetOutputs, service::helper::Timeout(TIMEOUT_MS)
    );
    BROOKESIA_CHECK_FALSE_RETURN(result.has_value(), false, "Display output query failed");

    std::vector<DisplayHelper::OutputInfo> outputs;
    BROOKESIA_CHECK_FALSE_RETURN(
        BROOKESIA_DESCRIBE_FROM_JSON(boost::json::value(result.value()), outputs),
        false, "Display output parse failed"
    );
    BROOKESIA_CHECK_FALSE_RETURN(!outputs.empty(), false, "No display output");
    const auto &output = outputs.front();
    BROOKESIA_CHECK_FALSE_RETURN(output.width > 0 && output.height > 0, false, "Invalid display size");
    output_name_ = output.name;
    output_id_ = output.id;
    width_ = output.width;
    height_ = output.height;
    has_backlight_ = output.backlight.has_value();
    BROOKESIA_LOGI("Display ready: %1%x%2%", width_, height_);
    return true;
}

bool Display::start_lvgl(int core_id)
{
    gui::lvgl::DisplaySourceConfig config{};
    config.output_name = "";
    config.task_core_id = core_id;
    return gui::lvgl::DisplaySource::get_instance().start(config);
}

bool Display::activate_lvgl()
{
    return DisplayHelper::call_function_sync(
        DisplayHelper::FunctionId::SetActiveSourceRole,
        std::string(), std::string(gui::lvgl::DISPLAY_SOURCE_ROLE),
        service::helper::Timeout(TIMEOUT_MS)
    ).has_value();
}

bool Display::start_gestures()
{
    auto config = gesture_data_;
    config.enabled = true;
    return DisplayHelper::call_function_sync(
        DisplayHelper::FunctionId::SetTouchGestureConfig,
        static_cast<double>(output_id_),
        BROOKESIA_DESCRIBE_TO_JSON(config).as_object(),
        service::helper::Timeout(TIMEOUT_MS)
    ).has_value();
}

