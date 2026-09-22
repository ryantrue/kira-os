/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_log.h"
#include "kira/state_machine.hpp"

namespace {
constexpr char TAG[] = "kira_boot";
}

extern "C" void app_main(void)
{
    kira::StateMachine state;
    state.dispatch(kira::Event::BootCompleted);

    // The HAL/Super composition is intentionally the next bring-up change.
    // Keeping the product state machine executable now gives CI a stable seam
    // while display, audio and board-manager integration are completed.
    ESP_LOGI(TAG, "Kira OS booted; surface=%s", kira::to_string(state.current()));
}

