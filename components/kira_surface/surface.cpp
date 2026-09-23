/* SPDX-License-Identifier: Apache-2.0 */
#include <algorithm>
#include <cmath>

#include "brookesia/gui_lvgl.hpp"
#include "lvgl.h"
#include "kira/surface.hpp"

namespace kira {
namespace {
constexpr int32_t SPHERE_BASE = 250;

lv_color_t state_color(State state)
{
    switch (state) {
    case State::Listening: return lv_color_hex(0x38D9FF);
    case State::Thinking: return lv_color_hex(0xA875FF);
    case State::Speaking: return lv_color_hex(0xFF4FD8);
    case State::Offline: return lv_color_hex(0xFFB347);
    case State::Error: return lv_color_hex(0xFF4D5A);
    default: return lv_color_hex(0x667CFF);
    }
}

const char *state_text(State state)
{
    switch (state) {
    case State::Listening: return "Listening";
    case State::Thinking: return "Thinking";
    case State::Speaking: return "Speaking";
    case State::Offline: return "Offline - local wake remains active";
    case State::Error: return "Kira needs attention";
    default: return "Say Hey Kira or tap for apps";
    }
}
} // namespace

Surface &Surface::instance()
{
    static Surface surface;
    return surface;
}

bool Surface::start()
{
    if (root_ != nullptr) return true;
    if (!state_machine_.dispatch(Event::BootCompleted)) return false;
    requested_state_.store(State::Idle, std::memory_order_relaxed);
    esp_brookesia::gui::lvgl::lock_thread();
    create_locked();
    esp_brookesia::gui::lvgl::unlock_thread();
    return root_ != nullptr && timer_ != nullptr;
}

void Surface::set_audio_level(float level) noexcept
{
    audio_level_.store(std::clamp(level, 0.0F, 1.0F), std::memory_order_relaxed);
}

void Surface::set_state(State state) noexcept
{
    requested_state_.store(state, std::memory_order_relaxed);
}

void Surface::create_locked()
{
    lv_obj_t *parent = lv_display_get_layer_top(lv_display_get_default());
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x03040A), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(root_, on_tap, LV_EVENT_CLICKED, this);

    halo_ = lv_obj_create(root_);
    sphere_ = lv_obj_create(root_);
    core_ = lv_obj_create(root_);
    for (auto *object : {halo_, sphere_, core_}) {
        lv_obj_remove_style_all(object);
        lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
        lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(object, LV_ALIGN_CENTER, 0, -28);
    }

    lv_obj_set_size(halo_, SPHERE_BASE + 96, SPHERE_BASE + 96);
    lv_obj_set_style_bg_opa(halo_, LV_OPA_20, 0);
    lv_obj_set_style_shadow_width(halo_, 72, 0);
    lv_obj_set_style_shadow_opa(halo_, LV_OPA_50, 0);

    lv_obj_set_size(sphere_, SPHERE_BASE, SPHERE_BASE);
    lv_obj_set_style_shadow_width(sphere_, 48, 0);
    lv_obj_set_style_shadow_opa(sphere_, LV_OPA_70, 0);

    lv_obj_set_size(core_, 108, 108);
    lv_obj_set_style_bg_color(core_, lv_color_hex(0xEAF7FF), 0);
    lv_obj_set_style_bg_opa(core_, LV_OPA_70, 0);
    lv_obj_set_style_shadow_color(core_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_shadow_width(core_, 36, 0);
    lv_obj_set_style_shadow_opa(core_, LV_OPA_70, 0);

    auto *title = lv_label_create(root_);
    lv_label_set_text(title, "KIRA");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF6F7FF), 0);
    lv_obj_set_style_text_letter_space(title, 8, 0);
    lv_obj_align(title, LV_ALIGN_BOTTOM_MID, 0, -94);

    status_ = lv_label_create(root_);
    lv_obj_set_width(status_, LV_PCT(80));
    lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_, lv_color_hex(0x9AA3BE), 0);
    lv_obj_align(status_, LV_ALIGN_BOTTOM_MID, 0, -50);

    apply_state_locked(State::Idle);
    timer_ = lv_timer_create(on_timer, 16, this);
}

void Surface::on_timer(lv_timer_t *timer)
{
    auto *self = static_cast<Surface *>(lv_timer_get_user_data(timer));
    if (self != nullptr) self->update_locked();
}

void Surface::on_tap(lv_event_t *event)
{
    auto *self = static_cast<Surface *>(lv_event_get_user_data(event));
    if (self != nullptr) self->reveal_desktop_locked();
}

void Surface::update_locked()
{
    if (root_ == nullptr) return;
    const State requested = requested_state_.load(std::memory_order_relaxed);
    if (requested != visual_state_) apply_state_locked(requested);

    const float target = audio_level_.load(std::memory_order_relaxed);
    smoothed_level_ += (target - smoothed_level_) * (target > smoothed_level_ ? 0.34F : 0.08F);
    phase_ += 0.055F;
    const float breath = (std::sin(phase_) + 1.0F) * 0.5F;
    const float pulse = std::max(smoothed_level_, breath * 0.075F);
    const int32_t diameter = SPHERE_BASE + static_cast<int32_t>(pulse * 72.0F);
    const int32_t halo = diameter + 96 + static_cast<int32_t>(pulse * 48.0F);
    lv_obj_set_size(sphere_, diameter, diameter);
    lv_obj_align(sphere_, LV_ALIGN_CENTER, 0, -28);
    lv_obj_set_size(halo_, halo, halo);
    lv_obj_align(halo_, LV_ALIGN_CENTER, 0, -28);
    lv_obj_set_style_opa(sphere_, static_cast<lv_opa_t>(210 + pulse * 45), 0);
}

void Surface::apply_state_locked(State state)
{
    visual_state_ = state;
    if (state == State::Desktop) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root_);
    const lv_color_t color = state_color(state);
    lv_obj_set_style_bg_color(halo_, color, 0);
    lv_obj_set_style_shadow_color(halo_, color, 0);
    lv_obj_set_style_bg_color(sphere_, color, 0);
    lv_obj_set_style_shadow_color(sphere_, color, 0);
    lv_label_set_text(status_, state_text(state));
}

void Surface::reveal_desktop_locked()
{
    if (root_ == nullptr) return;
    if (!state_machine_.dispatch(Event::ScreenTapped)) return;
    requested_state_.store(State::Desktop, std::memory_order_relaxed);
    apply_state_locked(State::Desktop);
}

} // namespace kira

