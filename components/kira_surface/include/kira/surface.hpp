/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <atomic>
#include "kira/state_machine.hpp"

struct _lv_obj_t;
struct _lv_timer_t;
struct _lv_event_t;

namespace kira {

class Surface final {
public:
    static Surface &instance();
    bool start();
    void set_audio_level(float normalized_level) noexcept;
    void set_state(State state) noexcept;
    [[nodiscard]] State state() const noexcept
    {
        return requested_state_.load(std::memory_order_relaxed);
    }

private:
    Surface() = default;
    static void on_timer(_lv_timer_t *timer);
    static void on_tap(_lv_event_t *event);
    void create_locked();
    void update_locked();
    void reveal_desktop_locked();
    void apply_state_locked(State state);

    StateMachine state_machine_;
    std::atomic<float> audio_level_{0.0F};
    std::atomic<State> requested_state_{State::Booting};
    State visual_state_ = State::Booting;
    _lv_obj_t *root_ = nullptr;
    _lv_obj_t *halo_ = nullptr;
    _lv_obj_t *sphere_ = nullptr;
    _lv_obj_t *core_ = nullptr;
    _lv_obj_t *status_ = nullptr;
    _lv_timer_t *timer_ = nullptr;
    float smoothed_level_ = 0.0F;
    float phase_ = 0.0F;
};

} // namespace kira

