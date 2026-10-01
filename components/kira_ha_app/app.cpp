/* SPDX-License-Identifier: Apache-2.0 */
#include <array>
#include <memory>
#include <string>
#include "brookesia/gui_lvgl.hpp"
#include "brookesia/system_core.hpp"
#include "kira/home_assistant_app.hpp"
#include "kira/platform/home_assistant.hpp"
#include "kira/platform/version.hpp"
#include "lvgl.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace kira::home_assistant_app {
namespace {
namespace core = esp_brookesia::system::core;
namespace ha = kira::platform::home_assistant;
constexpr const char *APP_ID = "kira.home_assistant";
constexpr const char *TAG = "kira_ha_app";
void log_perf(const char *event, int64_t started_us) { ESP_LOGI(TAG, "perf %s: %lld ms, heap=%u, psram=%u", event, static_cast<long long>((esp_timer_get_time()-started_us)/1000), static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)), static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM))); }

class HomeAssistantApp final : public core::IApp {
public:
    core::AppManifest get_manifest() const override {
        return {.id=APP_ID,.name="Home Assistant",.localized_names={{"en","Home Assistant"}},.version=kira::platform::firmware_version(),
            .kind=core::AppKind::Native,.visible=true,.preload_dom=false,.icon_id={},.supported_systems={},.icon_path={},
            .runtime_type=esp_brookesia::runtime::BackendType::Unknown,.app_path={},.entry={},.resource_dir={},.arguments={}};
    }
    std::expected<void,std::string> on_start(core::AppContext &context) override {
        const int64_t started_us=esp_timer_get_time(); context_=&context; esp_brookesia::gui::lvgl::lock_thread(); create_locked(); esp_brookesia::gui::lvgl::unlock_thread();
        ha::refresh_async(); if(root_){ log_perf("start",started_us); return {}; } return std::unexpected("Failed to create Home Assistant surface");
    }
    std::expected<void,std::string> on_pause(core::AppContext &) override { const int64_t t=esp_timer_get_time(); set_hidden(true); log_perf("pause",t); return {}; }
    std::expected<void,std::string> on_resume(core::AppContext &) override { const int64_t t=esp_timer_get_time(); set_hidden(false); ha::refresh_async(); log_perf("resume",t); return {}; }
    std::expected<void,std::string> on_stop(core::AppContext &) override {
        const int64_t t=esp_timer_get_time(); esp_brookesia::gui::lvgl::lock_thread(); if(timer_) lv_timer_delete(timer_); timer_=nullptr; if(root_) lv_obj_delete(root_); root_=nullptr; esp_brookesia::gui::lvgl::unlock_thread(); context_=nullptr; log_perf("stop",t); return {};
    }
private:
    struct Slot { HomeAssistantApp *self=nullptr; size_t index=0; };
    void set_hidden(bool hidden){ esp_brookesia::gui::lvgl::lock_thread(); if(root_){ if(hidden) lv_obj_add_flag(root_,LV_OBJ_FLAG_HIDDEN); else {lv_obj_clear_flag(root_,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(root_);} } esp_brookesia::gui::lvgl::unlock_thread(); }
    static void on_refresh(lv_event_t *e){ auto *self=static_cast<HomeAssistantApp*>(lv_event_get_user_data(e)); if(self) ha::refresh_async(); }
    static void on_close(lv_event_t *e){ auto *self=static_cast<HomeAssistantApp*>(lv_event_get_user_data(e)); if(self&&self->context_) (void)self->context_->system_service().request_close_app(self->context_->app_id()); }
    static void on_entity(lv_event_t *e){ auto *slot=static_cast<Slot*>(lv_event_get_user_data(e)); if(!slot||!slot->self) return; auto st=ha::status(); if(slot->index<st.entities.size()) ha::toggle_async(st.entities[slot->index].entity_id); }
    static void on_lv_timer(lv_timer_t *t){ auto *self=static_cast<HomeAssistantApp*>(lv_timer_get_user_data(t)); if(self) self->update_locked(); }
    void create_locked(){
        root_=lv_obj_create(lv_display_get_layer_top(lv_display_get_default())); lv_obj_set_size(root_,LV_PCT(100),LV_PCT(100)); lv_obj_set_style_bg_color(root_,lv_color_hex(0x0B1016),0); lv_obj_set_style_bg_opa(root_,LV_OPA_COVER,0); lv_obj_clear_flag(root_,LV_OBJ_FLAG_SCROLLABLE);
        auto *title=lv_label_create(root_); lv_label_set_text(title,"Home Assistant"); lv_obj_align(title,LV_ALIGN_TOP_MID,0,28);
        auto *close=lv_button_create(root_); lv_obj_align(close,LV_ALIGN_TOP_LEFT,18,18); lv_obj_add_event_cb(close,on_close,LV_EVENT_CLICKED,this); auto *cl=lv_label_create(close); lv_label_set_text(cl,LV_SYMBOL_CLOSE); lv_obj_center(cl);
        auto *refresh=lv_button_create(root_); lv_obj_align(refresh,LV_ALIGN_TOP_RIGHT,-18,18); lv_obj_add_event_cb(refresh,on_refresh,LV_EVENT_CLICKED,this); auto *rl=lv_label_create(refresh); lv_label_set_text(rl,LV_SYMBOL_REFRESH); lv_obj_center(rl);
        status_=lv_label_create(root_); lv_obj_set_width(status_,LV_PCT(90)); lv_obj_set_style_text_align(status_,LV_TEXT_ALIGN_CENTER,0); lv_obj_align(status_,LV_ALIGN_TOP_MID,0,90);
        list_=lv_obj_create(root_); lv_obj_set_size(list_,LV_PCT(92),520); lv_obj_align(list_,LV_ALIGN_BOTTOM_MID,0,-18); lv_obj_set_flex_flow(list_,LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(list_,8,0);
        for(size_t i=0;i<buttons_.size();++i){ slots_[i]={this,i}; buttons_[i]=lv_button_create(list_); lv_obj_set_width(buttons_[i],LV_PCT(100)); lv_obj_add_event_cb(buttons_[i],on_entity,LV_EVENT_CLICKED,&slots_[i]); labels_[i]=lv_label_create(buttons_[i]); lv_label_set_text(labels_[i],""); lv_obj_center(labels_[i]); }
        timer_=lv_timer_create(on_lv_timer,1000,this); update_locked();
    }
    void update_locked(){
        auto st=ha::status(); std::string status=(st.busy?"Working... ":st.connected?"Connected. ":"Disconnected. ")+st.message; lv_label_set_text(status_,status.c_str());
        for(size_t i=0;i<buttons_.size();++i){ if(i<st.entities.size()){const auto &e=st.entities[i]; std::string text=e.name+"  ·  "+e.state; lv_label_set_text(labels_[i],text.c_str()); lv_obj_clear_flag(buttons_[i],LV_OBJ_FLAG_HIDDEN);} else lv_obj_add_flag(buttons_[i],LV_OBJ_FLAG_HIDDEN); }
    }
    core::AppContext *context_=nullptr; lv_obj_t *root_=nullptr,*status_=nullptr,*list_=nullptr; lv_timer_t *timer_=nullptr;
    std::array<lv_obj_t*,8> buttons_{}; std::array<lv_obj_t*,8> labels_{}; std::array<Slot,8> slots_{};
};
class Provider final: public core::IAppProvider { public: core::AppManifest get_manifest() const override{return HomeAssistantApp().get_manifest();} std::shared_ptr<core::IApp> create_app() override{return std::make_shared<HomeAssistantApp>();}};
BROOKESIA_SYSTEM_CORE_APP_PROVIDER_REGISTER_WITH_SYMBOL(Provider,APP_ID,kira_home_assistant_app_provider_symbol);
}
void ensure_linked(){}
}
