#include "kira/boot.hpp"
#include <cstdint>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "kira/recovery_protocol.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace kira::boot {
namespace {
constexpr const char *TAG = "kira_boot";
esp_timer_handle_t s_stable_timer = nullptr;
bool s_started = false;
bool abnormal_reset(esp_reset_reason_t reason) {
    switch (reason) {
    case ESP_RST_PANIC: case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return true;
    default: return false;
    }
}
esp_err_t store_u8(const char *key, uint8_t value) {
    nvs_handle_t h; esp_err_t e=nvs_open(KIRA_RECOVERY_NVS_NAMESPACE,NVS_READWRITE,&h);
    if(e!=ESP_OK)return e; e=nvs_set_u8(h,key,value); if(e==ESP_OK)e=nvs_commit(h); nvs_close(h); return e;
}
uint8_t load_u8(const char *key) {
    nvs_handle_t h; uint8_t v=0;
    if(nvs_open(KIRA_RECOVERY_NVS_NAMESPACE,NVS_READONLY,&h)==ESP_OK){nvs_get_u8(h,key,&v);nvs_close(h);} return v;
}
esp_err_t reboot_into_recovery(kira_recovery_action_t action) {
    const esp_partition_t *p=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_FACTORY,nullptr);
    if(!p){ESP_LOGE(TAG,"no recovery partition");return ESP_ERR_NOT_FOUND;}
    esp_err_t e=store_u8(KIRA_RECOVERY_KEY_ACTION,static_cast<uint8_t>(action)); if(e!=ESP_OK)return e;
    e=esp_ota_set_boot_partition(p); if(e!=ESP_OK)return e; esp_restart(); return ESP_OK;
}
void on_stable(void *) { confirm_now(); store_u8(KIRA_RECOVERY_KEY_CRASHES,0); ESP_LOGI(TAG,"system stable"); }
}
bool pending_verification() {
    const esp_partition_t *p=esp_ota_get_running_partition(); esp_ota_img_states_t s;
    return p && esp_ota_get_state_partition(p,&s)==ESP_OK && s==ESP_OTA_IMG_PENDING_VERIFY;
}
esp_err_t confirm_now() {
    if(!pending_verification())return ESP_OK;
    esp_err_t e=esp_ota_mark_app_valid_cancel_rollback();
    if(e==ESP_OK)ESP_LOGI(TAG,"update confirmed, rollback cancelled"); return e;
}
esp_err_t start(std::chrono::seconds stable_for) {
    if(s_started)return ESP_ERR_INVALID_STATE; s_started=true;
    esp_err_t e=nvs_flash_init();
    if(e==ESP_OK){
        uint8_t crashes=abnormal_reset(esp_reset_reason())?static_cast<uint8_t>(load_u8(KIRA_RECOVERY_KEY_CRASHES)+1):0;
        store_u8(KIRA_RECOVERY_KEY_CRASHES,crashes);
        if(crashes>=KIRA_RECOVERY_CRASH_LOOP_THRESHOLD){store_u8(KIRA_RECOVERY_KEY_CRASHES,0);reboot_into_recovery(KIRA_RECOVERY_ACTION_CRASH_LOOP);}
    } else ESP_LOGW(TAG,"NVS unavailable (%s)",esp_err_to_name(e));
    esp_timer_create_args_t args={}; args.callback=&on_stable; args.name="kira_boot_stable";
    e=esp_timer_create(&args,&s_stable_timer); if(e!=ESP_OK)return e;
    auto us=std::chrono::duration_cast<std::chrono::microseconds>(stable_for).count();
    return esp_timer_start_once(s_stable_timer,static_cast<uint64_t>(us));
}
esp_err_t request_install(){return reboot_into_recovery(KIRA_RECOVERY_ACTION_INSTALL);}
esp_err_t request_restore(){return reboot_into_recovery(KIRA_RECOVERY_ACTION_RESTORE);}
esp_err_t request_recovery(){return reboot_into_recovery(KIRA_RECOVERY_ACTION_STAY);}
}
