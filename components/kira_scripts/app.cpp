/* SPDX-License-Identifier: Apache-2.0 */
#include <array>
#include <memory>
#include <string>
#include "brookesia/gui_lvgl.hpp"
#include "brookesia/system_core.hpp"
#include "kira/platform/scripts.hpp"
#include "kira/platform/version.hpp"
#include "kira/scripts_app.hpp"
#include "lvgl.h"
namespace kira::scripts_app { namespace { namespace core=esp_brookesia::system::core; namespace scripts=kira::platform::scripts; constexpr const char* APP_ID="kira.scripts"; constexpr size_t MAX_SCRIPTS=8;
class ScriptsApp final:public core::IApp { public:
 core::AppManifest get_manifest()const override{return{.id=APP_ID,.name="Scripts",.localized_names={{"en","Scripts"}},.version=kira::platform::firmware_version(),.kind=core::AppKind::Native,.visible=true,.preload_dom=false,.icon_id={},.supported_systems={},.icon_path={},.runtime_type=esp_brookesia::runtime::BackendType::Unknown,.app_path={},.entry={},.resource_dir={},.arguments={}};}
 std::expected<void,std::string> on_start(core::AppContext& c)override{ctx_=&c;esp_brookesia::gui::lvgl::lock_thread();create();esp_brookesia::gui::lvgl::unlock_thread();return root_?std::expected<void,std::string>{}:std::unexpected("Failed to create Scripts surface");}
 std::expected<void,std::string> on_pause(core::AppContext&)override{show(false);return{};} std::expected<void,std::string> on_resume(core::AppContext&)override{show(true);return{};}
 std::expected<void,std::string> on_stop(core::AppContext&)override{esp_brookesia::gui::lvgl::lock_thread();if(timer_)lv_timer_delete(timer_);timer_=nullptr;if(root_)lv_obj_delete(root_);root_=nullptr;esp_brookesia::gui::lvgl::unlock_thread();ctx_=nullptr;return{};}
 private: struct Slot{ScriptsApp*self;size_t index;};
 static void close_cb(lv_event_t*e){auto*s=static_cast<ScriptsApp*>(lv_event_get_user_data(e));if(s&&s->ctx_)(void)s->ctx_->system_service().request_close_app(s->ctx_->app_id());}
 static void run_cb(lv_event_t*e){auto*slot=static_cast<Slot*>(lv_event_get_user_data(e));if(!slot||!slot->self)return;const auto& cat=scripts::catalog();if(slot->index<cat.size())scripts::run_async(cat[slot->index].id);}
 static void timer_cb(lv_timer_t*t){auto*s=static_cast<ScriptsApp*>(lv_timer_get_user_data(t));if(s)s->refresh();}
 void show(bool v){esp_brookesia::gui::lvgl::lock_thread();if(root_){if(v){lv_obj_clear_flag(root_,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(root_);refresh();if(timer_){lv_timer_resume(timer_);lv_timer_ready(timer_);}}else{lv_obj_add_flag(root_,LV_OBJ_FLAG_HIDDEN);if(timer_)lv_timer_pause(timer_);}}esp_brookesia::gui::lvgl::unlock_thread();}
 void create(){root_=lv_obj_create(lv_display_get_layer_top(lv_display_get_default()));lv_obj_set_size(root_,LV_PCT(100),LV_PCT(100));lv_obj_set_style_bg_color(root_,lv_color_hex(0x0B1016),0);lv_obj_set_style_bg_opa(root_,LV_OPA_COVER,0);lv_obj_clear_flag(root_,LV_OBJ_FLAG_SCROLLABLE);auto*title=lv_label_create(root_);lv_label_set_text(title,"Scripts");lv_obj_align(title,LV_ALIGN_TOP_MID,0,28);auto*close=lv_button_create(root_);lv_obj_align(close,LV_ALIGN_TOP_LEFT,18,18);lv_obj_add_event_cb(close,close_cb,LV_EVENT_CLICKED,this);auto*cl=lv_label_create(close);lv_label_set_text(cl,LV_SYMBOL_CLOSE);lv_obj_center(cl);status_=lv_label_create(root_);lv_obj_set_width(status_,LV_PCT(88));lv_obj_set_style_text_align(status_,LV_TEXT_ALIGN_CENTER,0);lv_obj_align(status_,LV_ALIGN_TOP_MID,0,82);list_=lv_obj_create(root_);lv_obj_set_size(list_,LV_PCT(92),530);lv_obj_align(list_,LV_ALIGN_BOTTOM_MID,0,-18);lv_obj_set_flex_flow(list_,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(list_,8,0);const auto&cat=scripts::catalog();for(size_t i=0;i<MAX_SCRIPTS;i++){slots_[i]={this,i};buttons_[i]=lv_button_create(list_);lv_obj_set_width(buttons_[i],LV_PCT(100));lv_obj_set_height(buttons_[i],92);lv_obj_add_event_cb(buttons_[i],run_cb,LV_EVENT_CLICKED,&slots_[i]);labels_[i]=lv_label_create(buttons_[i]);lv_obj_set_width(labels_[i],LV_PCT(92));lv_label_set_long_mode(labels_[i],LV_LABEL_LONG_WRAP);if(i<cat.size()){const auto&s=cat[i];std::string text=s.name+"\n"+s.description+(s.configured?"":"\nNot configured");lv_label_set_text(labels_[i],text.c_str());if(!s.configured)lv_obj_add_state(buttons_[i],LV_STATE_DISABLED);}else lv_obj_add_flag(buttons_[i],LV_OBJ_FLAG_HIDDEN);}timer_=lv_timer_create(timer_cb,500,this);refresh();}
 void refresh(){auto st=scripts::status();std::string t=st.script_id.empty()?"Ready":(st.running?"Running: ":"Last run: ")+st.script_id+" · "+st.message;if(st.total_steps)t+=" · "+std::to_string(st.completed_steps)+"/"+std::to_string(st.total_steps);lv_label_set_text(status_,t.c_str());}
 core::AppContext*ctx_=nullptr;lv_obj_t*root_=nullptr,*status_=nullptr,*list_=nullptr;lv_timer_t*timer_=nullptr;std::array<lv_obj_t*,MAX_SCRIPTS>buttons_{};std::array<lv_obj_t*,MAX_SCRIPTS>labels_{};std::array<Slot,MAX_SCRIPTS>slots_{};
};
class Provider final:public core::IAppProvider{public:core::AppManifest get_manifest()const override{return ScriptsApp().get_manifest();}std::shared_ptr<core::IApp>create_app()override{return std::make_shared<ScriptsApp>();}};
BROOKESIA_SYSTEM_CORE_APP_PROVIDER_REGISTER_WITH_SYMBOL(Provider,APP_ID,kira_scripts_app_provider_symbol);
} void ensure_linked(){} }
