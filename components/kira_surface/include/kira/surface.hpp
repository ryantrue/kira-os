/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <atomic>
#include <functional>
#include "kira/state_machine.hpp"

struct _lv_obj_t;
struct _lv_timer_t;
struct _lv_event_t;

namespace kira {

class Surface final {
public:
    static Surface &instance();

    // The surface is owned by the native Kira application lifecycle.
    bool start();
    void stop();
    void pause();
    void resume();

    void set_close_handler(std::function<void()> handler);
    void set_settings_handler(std::function<void()> handler);
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
    static void on_settings(_lv_event_t *event);
    void create_locked();
    void destroy_locked();
    void update_locked();
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
    std::function<void()> close_handler_;
    std::function<void()> settings_handler_;
    bool paused_ = false;
    float smoothed_level_ = 0.0F;
    float phase_ = 0.0F;
};

} // namespace kira
