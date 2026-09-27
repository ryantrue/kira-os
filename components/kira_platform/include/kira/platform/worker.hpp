/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <functional>
#include <string>

namespace kira::platform {

// One background task for slow Kira Center jobs (network, SD card, flash).
// Jobs run one at a time, off the LVGL thread, on an internal-RAM stack so
// they may touch flash. Jobs publish results into their module's state; the
// UI polls that state and never receives calls from this task.
bool submit_job(const char *name, std::function<void()> job);

// Name of the running job, empty when idle.
[[nodiscard]] std::string current_job();

}  // namespace kira::platform
