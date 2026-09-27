/* SPDX-License-Identifier: Apache-2.0 */
#include "kira/center.hpp"

#include <cinttypes>
#include <cstdio>
#include <string>
#include <vector>

#include "brookesia/gui_lvgl.hpp"
#include "lvgl.h"

#include "kira/audio.hpp"
#include "kira/platform/ai_providers.hpp"
#include "kira/platform/home_assistant.hpp"
#include "kira/platform/log_share.hpp"
#include "kira/platform/logger.hpp"
#include "kira/platform/sdcard.hpp"
#include "kira/platform/settings.hpp"
#include "kira/platform/updater.hpp"
#include "kira/platform/version.hpp"
#include "kira/platform/worker.hpp"
#include "kira/surface.hpp"
#include "sdkconfig.h"

namespace kira::center {
namespace {

namespace platform = kira::platform;
using platform::Settings;

// ---------------------------------------------------------------- style ----

#if LV_FONT_MONTSERRAT_20
const lv_font_t *const FONT_BODY = &lv_font_montserrat_20;
#else
const lv_font_t *const FONT_BODY = LV_FONT_DEFAULT;
#endif
#if LV_FONT_MONTSERRAT_28
const lv_font_t *const FONT_TITLE = &lv_font_montserrat_28;
#else
const lv_font_t *const FONT_TITLE = FONT_BODY;
#endif

constexpr uint32_t COLOR_BG = 0x0B0E17;
constexpr uint32_t COLOR_PANEL = 0x151A28;
constexpr uint32_t COLOR_TEXT = 0xE8ECF8;
constexpr uint32_t COLOR_MUTED = 0x8C95B0;
constexpr uint32_t COLOR_ACCENT = 0x667CFF;
constexpr uint32_t COLOR_DANGER = 0xE5484D;
constexpr int32_t ROW_HEIGHT = 64;

// Text fields that are saved when the keyboard's OK key is pressed.
enum class Field : uintptr_t {
    AiModel = 1,
    AiKey,
    AiEndpoint,
    HaUrl,
    HaToken,
};

// ------------------------------------------------------------- widgets ----

lv_obj_t *label(lv_obj_t *parent, const char *text, uint32_t color = COLOR_TEXT)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(obj, lv_pct(100));
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(obj, FONT_BODY, 0);
    return obj;
}

lv_obj_t *heading(lv_obj_t *parent, const char *text)
{
    lv_obj_t *obj = label(parent, text, COLOR_ACCENT);
    lv_obj_set_style_pad_top(obj, 8, 0);
    return obj;
}

lv_obj_t *button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, uint32_t color = COLOR_ACCENT)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, lv_pct(100), ROW_HEIGHT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *text_obj = lv_label_create(btn);
    lv_label_set_text(text_obj, text);
    lv_obj_set_style_text_font(text_obj, FONT_BODY, 0);
    lv_obj_center(text_obj);
    return btn;
}

void set_button_text(lv_obj_t *btn, const char *text)
{
    lv_obj_t *text_obj = lv_obj_get_child(btn, 0);
    if (text_obj != nullptr) {
        lv_label_set_text(text_obj, text);
    }
}

void set_enabled(lv_obj_t *obj, bool enabled)
{
    if (enabled) {
        lv_obj_remove_state(obj, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(obj, LV_STATE_DISABLED);
    }
}

// A row with a text on the left and a switch on the right.
lv_obj_t *switch_row(lv_obj_t *parent, const char *text, bool checked, lv_event_cb_t cb, void *user = nullptr)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), ROW_HEIGHT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *text_obj = label(row, text);
    lv_obj_set_flex_grow(text_obj, 1);
    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 84, 44);
    if (checked) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, user);
    return sw;
}

lv_obj_t *dropdown(lv_obj_t *parent, const char *options, uint32_t selected, lv_event_cb_t cb)
{
    lv_obj_t *dd = lv_dropdown_create(parent);
    lv_obj_set_width(dd, lv_pct(100));
    lv_dropdown_set_options(dd, options);
    lv_dropdown_set_selected(dd, selected);
    lv_obj_set_style_text_font(dd, FONT_BODY, 0);
    lv_obj_t *list = lv_dropdown_get_list(dd);
    if (list != nullptr) {
        lv_obj_set_style_text_font(list, FONT_BODY, 0);
    }
    lv_obj_add_event_cb(dd, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    return dd;
}

std::string format_bytes(uint64_t bytes)
{
    char text[32];
    if (bytes >= (1ULL << 30)) {
        snprintf(text, sizeof(text), "%.1f GB", static_cast<double>(bytes) / (1ULL << 30));
    } else if (bytes >= (1ULL << 20)) {
        snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1ULL << 20));
    } else {
        snprintf(text, sizeof(text), "%" PRIu64 " KB", bytes / 1024);
    }
    return text;
}

// ---------------------------------------------------------------- state ----

struct Ui {
    lv_obj_t *root = nullptr;
    lv_obj_t *keyboard = nullptr;
    lv_timer_t *poll = nullptr;
    // Kira
    lv_obj_t *idle_dropdown = nullptr;
    lv_obj_t *speaker_status = nullptr;
    // AI
    lv_obj_t *ai_model = nullptr;
    lv_obj_t *ai_key = nullptr;
    lv_obj_t *ai_key_hint = nullptr;
    lv_obj_t *ai_endpoint = nullptr;
    // Home Assistant
    lv_obj_t *ha_status = nullptr;
    lv_obj_t *ha_list = nullptr;
    std::vector<std::string> ha_ids;
    uint32_t ha_generation = UINT32_MAX;
    // SD
    lv_obj_t *sd_info = nullptr;
    lv_obj_t *sd_format = nullptr;
    uint32_t sd_generation = UINT32_MAX;
    // Logs
    lv_obj_t *log_info = nullptr;
    lv_obj_t *log_share_button = nullptr;
    lv_obj_t *log_share_info = nullptr;
    // Updates
    lv_obj_t *update_info = nullptr;
    lv_obj_t *update_bar = nullptr;
    lv_obj_t *update_install = nullptr;
    lv_obj_t *update_check = nullptr;
    uint32_t update_generation = UINT32_MAX;
};

Ui s_ui;
lv_obj_t *s_fab = nullptr;
lv_timer_t *s_assistant_timer = nullptr;

void close_locked();

// ------------------------------------------------------------- keyboard ----

void hide_keyboard()
{
    if (s_ui.keyboard != nullptr) {
        lv_keyboard_set_textarea(s_ui.keyboard, nullptr);
        lv_obj_add_flag(s_ui.keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

void save_field(lv_obj_t *ta)
{
    const auto field = static_cast<Field>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(ta)));
    const std::string value = lv_textarea_get_text(ta);
    auto &settings = Settings::instance();
    switch (field) {
    case Field::AiModel:
        settings.set_ai_model(value);
        break;
    case Field::AiEndpoint:
        settings.set_ai_endpoint(value);
        break;
    case Field::HaUrl:
        settings.set_ha_url(value);
        break;
    case Field::AiKey:
        settings.set_ai_key(settings.ai_provider(), value);
        lv_textarea_set_text(ta, "");  // secrets are not kept on screen
        lv_textarea_set_placeholder_text(ta, value.empty() ? "not set" : "saved (hidden)");
        break;
    case Field::HaToken:
        settings.set_ha_token(value);
        lv_textarea_set_text(ta, "");
        lv_textarea_set_placeholder_text(ta, value.empty() ? "not set" : "saved (hidden)");
        break;
    }
}

void on_keyboard(lv_event_t *e)
{
    lv_obj_t *ta = lv_keyboard_get_textarea(s_ui.keyboard);
    if (lv_event_get_code(e) == LV_EVENT_READY && ta != nullptr) {
        save_field(ta);
    }
    hide_keyboard();
}

void on_textarea_focus(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target_obj(e);
    lv_keyboard_set_textarea(s_ui.keyboard, ta);
    lv_obj_remove_flag(s_ui.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_ui.keyboard);
    lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
}

lv_obj_t *text_field(lv_obj_t *parent, Field field, const std::string &value, const char *placeholder,
                     bool secret)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_width(ta, lv_pct(100));
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, secret);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_text(ta, value.c_str());
    lv_obj_set_style_text_font(ta, FONT_BODY, 0);
    lv_obj_set_user_data(ta, reinterpret_cast<void *>(static_cast<uintptr_t>(field)));
    lv_obj_add_event_cb(ta, on_textarea_focus, LV_EVENT_FOCUSED, nullptr);
    return ta;
}

void confirm(const char *title, const char *text, const char *action, lv_event_cb_t on_confirm)
{
    lv_obj_t *box = lv_msgbox_create(nullptr);
    lv_msgbox_add_title(box, title);
    lv_msgbox_add_text(box, text);
    lv_obj_t *ok = lv_msgbox_add_footer_button(box, action);
    lv_obj_t *cancel = lv_msgbox_add_footer_button(box, "Cancel");
    lv_obj_set_style_bg_color(ok, lv_color_hex(COLOR_DANGER), 0);
    lv_obj_add_event_cb(ok, on_confirm, LV_EVENT_CLICKED, box);
    lv_obj_add_event_cb(cancel, [](lv_event_t *e) {
        lv_msgbox_close_async(static_cast<lv_obj_t *>(lv_event_get_user_data(e)));
    }, LV_EVENT_CLICKED, box);
    lv_obj_set_style_text_font(box, FONT_BODY, 0);
    lv_obj_set_width(box, 560);
}

void close_confirm(lv_event_t *e)
{
    lv_msgbox_close_async(static_cast<lv_obj_t *>(lv_event_get_user_data(e)));
}

// ----------------------------------------------------------- Kira page ----

void build_kira_page(lv_obj_t *page)
{
    auto &settings = Settings::instance();
    heading(page, "Assistant");
    switch_row(page, "Start Kira automatically", settings.autostart(), [](lv_event_t *e) {
        const bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
        Settings::instance().set_autostart(on);
        set_enabled(s_ui.idle_dropdown, on);
    });
    label(page, "Show Kira after inactivity:", COLOR_MUTED);
    uint32_t idle_index = 1;
    for (size_t i = 0; i < Settings::IDLE_CHOICES_MIN.size(); ++i) {
        if (Settings::IDLE_CHOICES_MIN[i] == settings.idle_minutes()) {
            idle_index = static_cast<uint32_t>(i);
        }
    }
    s_ui.idle_dropdown = dropdown(page, "1 minute\n5 minutes\n15 minutes", idle_index, [](lv_event_t *e) {
        const uint32_t index = lv_dropdown_get_selected(lv_event_get_target_obj(e));
        if (index < Settings::IDLE_CHOICES_MIN.size()) {
            Settings::instance().set_idle_minutes(Settings::IDLE_CHOICES_MIN[index]);
        }
    });
    set_enabled(s_ui.idle_dropdown, settings.autostart());
    label(page, "Off: Kira stays hidden until you open it. Recovery, updates and the desktop "
          "are not affected.", COLOR_MUTED);
    button(page, "Open Kira now", [](lv_event_t *) {
        close_locked();
        kira::Surface::instance().show_locked();
    });

    heading(page, "Speaker");
    button(page, "Play test tone", [](lv_event_t *) {
        const bool ok = kira::audio::play_test_tone();
        lv_label_set_text(s_ui.speaker_status, ok ? "You should hear a one second tone."
                                                  : "Speaker unavailable; see the log.");
    });
    s_ui.speaker_status = label(page, "", COLOR_MUTED);
}

// ------------------------------------------------------------- AI page ----

void refresh_ai_fields()
{
    auto &settings = Settings::instance();
    const std::string provider_id = settings.ai_provider();
    const auto *provider = platform::find_ai_provider(provider_id);
    lv_textarea_set_text(s_ui.ai_model, settings.ai_model().c_str());
    const bool needs_key = provider != nullptr && provider->needs_api_key;
    const bool needs_endpoint = provider != nullptr && provider->needs_endpoint;
    for (lv_obj_t *obj : {s_ui.ai_key, s_ui.ai_key_hint}) {
        if (needs_key) {
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_textarea_set_text(s_ui.ai_key, "");
    lv_textarea_set_placeholder_text(s_ui.ai_key, settings.has_ai_key(provider_id) ? "saved (hidden)" : "not set");
    if (needs_endpoint) {
        lv_obj_remove_flag(s_ui.ai_endpoint, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_ui.ai_endpoint, LV_OBJ_FLAG_HIDDEN);
    }
}

void build_ai_page(lv_obj_t *page)
{
    auto &settings = Settings::instance();
    heading(page, "Thinking model");
    std::string options;
    uint32_t selected = 0;
    uint32_t index = 0;
    for (const auto &provider : platform::ai_providers()) {
        if (!options.empty()) {
            options += "\n";
        }
        options += std::string(provider.display_name);
        if (provider.id == settings.ai_provider()) {
            selected = index;
        }
        ++index;
    }
    dropdown(page, options.c_str(), selected, [](lv_event_t *e) {
        const uint32_t i = lv_dropdown_get_selected(lv_event_get_target_obj(e));
        const auto providers = platform::ai_providers();
        if (i < providers.size()) {
            Settings::instance().set_ai_provider(providers[i].id);
            refresh_ai_fields();
        }
    });
    label(page, "Model", COLOR_MUTED);
    s_ui.ai_model = text_field(page, Field::AiModel, settings.ai_model(), "model name", false);
    s_ui.ai_key_hint = label(page, "API key", COLOR_MUTED);
    s_ui.ai_key = text_field(page, Field::AiKey, "", "not set", true);
    s_ui.ai_endpoint = text_field(page, Field::AiEndpoint, settings.ai_endpoint(),
                                  "http://kira-brain.local:8765", false);
    label(page, "Press OK on the keyboard to save a field. Keys are stored on the device and never "
          "shown or logged. The realtime voice session uses OpenAI or Kira Brain; the other "
          "providers are reached through Kira Brain (docs/assistant-architecture.md).", COLOR_MUTED);
    refresh_ai_fields();
}

// ---------------------------------------------------- Home Assistant page ----

void on_entity_switch(lv_event_t *e)
{
    const auto index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    if (index < s_ui.ha_ids.size()) {
        platform::home_assistant::toggle_async(s_ui.ha_ids[index]);
    }
}

void render_entities(const platform::home_assistant::Status &status)
{
    lv_obj_clean(s_ui.ha_list);
    s_ui.ha_ids.clear();
    for (const auto &entity : status.entities) {
        const uintptr_t index = s_ui.ha_ids.size();
        s_ui.ha_ids.push_back(entity.entity_id);
        lv_obj_t *sw = switch_row(s_ui.ha_list, entity.name.c_str(), entity.state == "on", on_entity_switch,
                                  reinterpret_cast<void *>(index));
        set_enabled(sw, entity.state == "on" || entity.state == "off");
    }
}

void build_home_page(lv_obj_t *page)
{
    auto &settings = Settings::instance();
    heading(page, "Home Assistant");
    label(page, "Address", COLOR_MUTED);
    text_field(page, Field::HaUrl, settings.ha_url(), "http://homeassistant.local:8123", false);
    label(page, "Long-lived access token", COLOR_MUTED);
    text_field(page, Field::HaToken, "", settings.has_ha_token() ? "saved (hidden)" : "not set", true);
    button(page, "Test connection", [](lv_event_t *) { platform::home_assistant::test_async(); });
    button(page, "Load lights and switches", [](lv_event_t *) { platform::home_assistant::refresh_async(); });
    s_ui.ha_status = label(page, "", COLOR_MUTED);
    s_ui.ha_list = lv_obj_create(page);
    lv_obj_remove_style_all(s_ui.ha_list);
    lv_obj_set_size(s_ui.ha_list, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_ui.ha_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_ui.ha_list, 4, 0);
}

// ------------------------------------------------------------- SD page ----

void build_sd_page(lv_obj_t *page)
{
    heading(page, "microSD card");
    s_ui.sd_info = label(page, "Checking...");
    button(page, "Refresh", [](lv_event_t *) { platform::sdcard::refresh_async(); });
    s_ui.sd_format = button(page, "Format card (FAT32)", [](lv_event_t *) {
        confirm("Format SD card", "Everything on the card will be erased and it will be formatted as FAT32. "
                "Continue?", "Format", [](lv_event_t *e) {
            close_confirm(e);
            platform::sdcard::format_async();
        });
    }, COLOR_DANGER);
    label(page, "Kira stores logs, update files and backups in the kira folder on the card. "
          "exFAT cards must be formatted here before use.", COLOR_MUTED);
    platform::sdcard::refresh_async();
}

// ------------------------------------------------------------ Logs page ----

void build_logs_page(lv_obj_t *page)
{
    heading(page, "Logging");
    switch_row(page, "Save system log", platform::logger::enabled(), [](lv_event_t *e) {
        platform::logger::set_enabled(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
    });
    s_ui.log_info = label(page, "", COLOR_MUTED);
    label(page, "Saved to the SD card when present, otherwise to system flash. At most 1 MB is kept; "
          "older lines are removed.", COLOR_MUTED);
    heading(page, "Download");
    s_ui.log_share_button = button(page, "Share log over Wi-Fi", [](lv_event_t *) {
        if (platform::log_share::status().running) {
            platform::log_share::stop();
        } else {
            platform::submit_job("log_share", [] { platform::log_share::start(); });
        }
    });
    s_ui.log_share_info = label(page, "", COLOR_TEXT);
    button(page, "Delete saved log", [](lv_event_t *) {
        confirm("Delete log", "Delete the saved log from the device?", "Delete", [](lv_event_t *e) {
            close_confirm(e);
            platform::submit_job("log_clear", [] { platform::logger::clear(); });
        });
    }, COLOR_DANGER);
}

// --------------------------------------------------------- Updates page ----

void build_updates_page(lv_obj_t *page)
{
    heading(page, "Kira OS");
    const std::string version = "Version " + platform::firmware_version() + "\nBuilt " +
                                platform::firmware_build_date() + "\nReleases: github.com/" +
                                CONFIG_KIRA_UPDATE_GITHUB_REPO;
    label(page, version.c_str());
    s_ui.update_check = button(page, "Check for updates", [](lv_event_t *) { platform::updater::check_async(); });
    s_ui.update_info = label(page, "", COLOR_MUTED);
    s_ui.update_bar = lv_bar_create(page);
    lv_obj_set_size(s_ui.update_bar, lv_pct(100), 16);
    lv_bar_set_range(s_ui.update_bar, 0, 100);
    lv_obj_add_flag(s_ui.update_bar, LV_OBJ_FLAG_HIDDEN);
    s_ui.update_install = button(page, "Download and install", [](lv_event_t *) {
        const auto status = platform::updater::status();
        const std::string text = "Install Kira " + status.latest + "? The update is saved to the SD card, "
                                 "Kira restarts into recovery and installs it. Keep the power connected.";
        confirm("Install update", text.c_str(), "Install", [](lv_event_t *e) {
            close_confirm(e);
            platform::updater::install_async();
        });
    });
    set_enabled(s_ui.update_install, false);
    label(page, "An SD card is required. If the new version fails to start, recovery restores the "
          "previous one.", COLOR_MUTED);
}

// ------------------------------------------------------------- polling ----

void poll(lv_timer_t *)
{
    if (s_ui.root == nullptr) {
        return;
    }
    // Home Assistant
    const auto ha = platform::home_assistant::status();
    if (ha.generation != s_ui.ha_generation) {
        s_ui.ha_generation = ha.generation;
        std::string text = ha.busy ? "Working..." : ha.message;
        if (!ha.configured && text.empty()) {
            text = "Enter the address and token, then press OK on the keyboard";
        }
        lv_label_set_text(s_ui.ha_status, text.c_str());
        render_entities(ha);
    }
    // SD card
    const auto sd = platform::sdcard::status();
    if (sd.generation != s_ui.sd_generation) {
        s_ui.sd_generation = sd.generation;
        std::string text;
        if (sd.mounted) {
            text = "Mounted at /sdcard (" + sd.filesystem + ")\nTotal " + format_bytes(sd.total_bytes) +
                   ", free " + format_bytes(sd.free_bytes) + ", used " +
                   format_bytes(sd.total_bytes - sd.free_bytes);
        } else {
            text = "Not available";
        }
        if (!sd.message.empty()) {
            text += "\n" + sd.message;
        }
        lv_label_set_text(s_ui.sd_info, text.c_str());
        set_enabled(s_ui.sd_format, !sd.busy);
    }
    // Logs
    const auto log = platform::logger::status();
    char log_text[160];
    snprintf(log_text, sizeof(log_text), "%s\nStored: %s%s",
             log.location.empty() ? "No storage available yet" : log.location.c_str(),
             format_bytes(log.stored_bytes).c_str(),
             log.dropped_lines ? ", some lines were dropped" : "");
    lv_label_set_text(s_ui.log_info, log_text);
    const auto share = platform::log_share::status();
    set_button_text(s_ui.log_share_button, share.running ? "Stop sharing" : "Share log over Wi-Fi");
    std::string share_text;
    if (share.running) {
        share_text = "Wi-Fi: " + share.ssid + "\nPassword: " + share.password + "\nOpen " + share.ap_url;
        if (!share.lan_url.empty()) {
            share_text += "\nor on your network: " + share.lan_url;
        }
        share_text += "\nThe log is deleted from the device after a full download.";
    } else {
        share_text = share.message;
    }
    lv_label_set_text(s_ui.log_share_info, share_text.c_str());
    // Updates
    const auto update = platform::updater::status();
    if (update.generation != s_ui.update_generation) {
        s_ui.update_generation = update.generation;
        lv_label_set_text(s_ui.update_info, update.message.c_str());
        using platform::updater::Phase;
        const bool downloading = update.phase == Phase::Downloading || update.phase == Phase::Verifying;
        if (downloading && update.total_bytes > 0) {
            lv_obj_remove_flag(s_ui.update_bar, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(s_ui.update_bar,
                             static_cast<int32_t>(100ULL * update.download_bytes / update.total_bytes), LV_ANIM_OFF);
        } else {
            lv_obj_add_flag(s_ui.update_bar, LV_OBJ_FLAG_HIDDEN);
        }
        set_enabled(s_ui.update_install, update.phase == Phase::Available);
        set_enabled(s_ui.update_check, !downloading && update.phase != Phase::Checking &&
                    update.phase != Phase::ReadyToInstall);
    }
}

// --------------------------------------------------------------- frame ----

lv_obj_t *add_page(lv_obj_t *tabview, const char *title)
{
    lv_obj_t *page = lv_tabview_add_tab(tabview, title);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(page, 20, 0);
    lv_obj_set_style_pad_row(page, 12, 0);
    lv_obj_set_style_pad_bottom(page, 340, 0);  // room to scroll fields above the keyboard
    return page;
}

void close_locked()
{
    if (s_ui.root == nullptr) {
        return;
    }
    if (s_ui.poll != nullptr) {
        lv_timer_delete(s_ui.poll);
    }
    lv_obj_delete_async(s_ui.root);  // may be called from an event of a child
    s_ui = Ui{};
}

void build_locked()
{
    lv_obj_t *root = lv_obj_create(lv_display_get_layer_top(lv_display_get_default()));
    s_ui.root = root;
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);  // swallow touches meant for the desktop below

    lv_obj_t *header = lv_obj_create(root);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, lv_pct(100), 80);
    lv_obj_set_style_bg_color(header, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Kira Center");
    lv_obj_set_style_text_font(title, FONT_TITLE, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 24, 0);
    lv_obj_t *close = lv_button_create(header);
    lv_obj_set_size(close, 64, 64);
    lv_obj_align(close, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_set_style_bg_color(close, lv_color_hex(0x252C42), 0);
    lv_obj_add_event_cb(close, [](lv_event_t *) { close_locked(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *close_icon = lv_label_create(close);
    lv_label_set_text(close_icon, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(close_icon, FONT_BODY, 0);
    lv_obj_center(close_icon);

    lv_obj_t *tabview = lv_tabview_create(root);
    lv_obj_set_size(tabview, lv_pct(100), lv_display_get_vertical_resolution(nullptr) - 80);
    lv_obj_align(tabview, LV_ALIGN_TOP_LEFT, 0, 80);
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_LEFT);
    lv_tabview_set_tab_bar_size(tabview, 170);
    lv_obj_set_style_bg_color(tabview, lv_color_hex(COLOR_BG), 0);
    lv_obj_t *bar = lv_tabview_get_tab_bar(tabview);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_text_font(bar, FONT_BODY, 0);
    lv_obj_set_style_text_color(bar, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_t *content = lv_tabview_get_content(tabview);
    lv_obj_set_style_text_color(content, lv_color_hex(COLOR_TEXT), 0);

    build_kira_page(add_page(tabview, "Kira"));
    build_ai_page(add_page(tabview, "AI"));
    build_home_page(add_page(tabview, "Home"));
    build_sd_page(add_page(tabview, "SD card"));
    build_logs_page(add_page(tabview, "Logs"));
    build_updates_page(add_page(tabview, "Updates"));

    s_ui.keyboard = lv_keyboard_create(root);
    lv_obj_set_size(s_ui.keyboard, lv_pct(100), 320);
    lv_obj_align(s_ui.keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(s_ui.keyboard, FONT_TITLE, 0);
    lv_obj_add_event_cb(s_ui.keyboard, on_keyboard, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(s_ui.keyboard, on_keyboard, LV_EVENT_CANCEL, nullptr);
    lv_obj_add_flag(s_ui.keyboard, LV_OBJ_FLAG_HIDDEN);

    s_ui.poll = lv_timer_create(poll, 500, nullptr);
    poll(nullptr);
}

// ------------------------------------------------ assistant controller ----

// Shows the Kira surface after the configured inactivity when auto-start is on.
// Runs on the LVGL thread (lv_timer).
void assistant_tick(lv_timer_t *)
{
    auto &settings = Settings::instance();
    if (!settings.autostart() || s_ui.root != nullptr) {
        return;
    }
    auto &surface = kira::Surface::instance();
    if (surface.visible_locked()) {
        return;
    }
    const uint32_t idle_ms = static_cast<uint32_t>(settings.idle_minutes()) * 60U * 1000U;
    if (lv_display_get_inactive_time(nullptr) >= idle_ms) {
        surface.show_locked();
    }
}

void create_fab_locked()
{
    // Right edge, upper middle: clear of the System Super keyboard (input bar at
    // the top, keys in the lower half) and of the status bar.
    s_fab = lv_button_create(lv_display_get_layer_top(lv_display_get_default()));
    lv_obj_set_size(s_fab, 60, 60);
    lv_obj_set_style_radius(s_fab, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_fab, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_bg_opa(s_fab, LV_OPA_50, 0);
    lv_obj_set_style_shadow_width(s_fab, 12, 0);
    lv_obj_align(s_fab, LV_ALIGN_RIGHT_MID, -10, -170);
    lv_obj_add_event_cb(s_fab, [](lv_event_t *) { open_locked(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *text = lv_label_create(s_fab);
    lv_label_set_text(text, "K");
    lv_obj_set_style_text_font(text, FONT_TITLE, 0);
    lv_obj_set_style_text_color(text, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(text);
}

}  // namespace

void open_locked()
{
    if (s_ui.root != nullptr) {
        lv_obj_move_foreground(s_ui.root);
        return;
    }
    build_locked();
}

void start()
{
    esp_brookesia::gui::lvgl::lock_thread();
    create_fab_locked();
    // The Kira surface is on the same layer; keep the button below it so a
    // visible surface covers it (the surface has its own settings button).
    lv_obj_move_background(s_fab);
    s_assistant_timer = lv_timer_create(assistant_tick, 1000, nullptr);
    esp_brookesia::gui::lvgl::unlock_thread();
    kira::Surface::instance().set_settings_handler([] { open_locked(); });
}

}  // namespace kira::center
