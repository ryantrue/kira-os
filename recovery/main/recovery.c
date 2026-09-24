#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "driver/sdmmc_host.h"
#include "esp_app_format.h"
#include "esp_err.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kira/recovery_protocol.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"

static const char *TAG="kira_recovery";
#define SD_SLOT SDMMC_HOST_SLOT_0
#define SD_BUS_WIDTH 4
#define SD_LDO_CHANNEL 4
#define IO_CHUNK (16*1024)
static uint8_t s_buf[IO_CHUNK];

static void __attribute__((noreturn)) stay(const char *r){for(;;){ESP_LOGW(TAG,"staying in recovery: %s",r);vTaskDelay(pdMS_TO_TICKS(10000));}}
static void __attribute__((noreturn)) reboot(const char *r){ESP_LOGI(TAG,"rebooting: %s",r);vTaskDelay(pdMS_TO_TICKS(200));esp_restart();for(;;){}}
static uint8_t take_action(void){uint8_t a=KIRA_RECOVERY_ACTION_NONE;nvs_handle_t h;if(nvs_open(KIRA_RECOVERY_NVS_NAMESPACE,NVS_READWRITE,&h)!=ESP_OK)return a;if(nvs_get_u8(h,KIRA_RECOVERY_KEY_ACTION,&a)==ESP_OK){nvs_erase_key(h,KIRA_RECOVERY_KEY_ACTION);nvs_commit(h);}nvs_close(h);return a;}
static bool mount_sd(void){
 sdmmc_host_t host=SDMMC_HOST_DEFAULT();host.slot=SD_SLOT;
 sd_pwr_ctrl_ldo_config_t lc={.ldo_chan_id=SD_LDO_CHANNEL};sd_pwr_ctrl_handle_t pc=NULL;
 esp_err_t e=sd_pwr_ctrl_new_on_chip_ldo(&lc,&pc);if(e!=ESP_OK){ESP_LOGE(TAG,"SD power control: %s",esp_err_to_name(e));return false;}host.pwr_ctrl_handle=pc;
 sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();slot.width=SD_BUS_WIDTH;
 const esp_vfs_fat_sdmmc_mount_config_t mc={.format_if_mount_failed=false,.max_files=4,.allocation_unit_size=16*1024};
 sdmmc_card_t *card=NULL;e=esp_vfs_fat_sdmmc_mount(KIRA_RECOVERY_SD_MOUNT,&host,&slot,&mc,&card);
 if(e!=ESP_OK){ESP_LOGW(TAG,"SD card not available: %s",esp_err_to_name(e));return false;}mkdir(KIRA_RECOVERY_DIR,0775);ESP_LOGI(TAG,"SD card mounted");return true;
}
static bool file_exists(const char *p){struct stat s;return stat(p,&s)==0;}
static size_t partition_image_len(const esp_partition_t *p,esp_app_desc_t *d){
 esp_partition_pos_t pos={.offset=p->address,.size=p->size};esp_image_metadata_t m;
 if(esp_image_verify(ESP_IMAGE_VERIFY_SILENT,&pos,&m)!=ESP_OK)return 0;
 if (esp_ota_get_partition_description(p, d) != ESP_OK ||
     strcmp(d->project_name, KIRA_RECOVERY_EXPECTED_PROJECT) != 0) {
  return 0;
 }
 return m.image_len;
}
static esp_err_t read_file_desc(const char *p,esp_app_desc_t *d,size_t *size){
 struct stat st;if(stat(p,&st)!=0)return ESP_ERR_NOT_FOUND;FILE *f=fopen(p,"rb");if(!f)return ESP_ERR_NOT_FOUND;
 esp_image_header_t h;esp_image_segment_header_t s;bool ok=fread(&h,1,sizeof(h),f)==sizeof(h)&&fread(&s,1,sizeof(s),f)==sizeof(s)&&fread(d,1,sizeof(*d),f)==sizeof(*d);fclose(f);
 if (!ok) {
  return ESP_ERR_INVALID_SIZE;
 }
 if (h.magic != ESP_IMAGE_HEADER_MAGIC || h.chip_id != ESP_CHIP_ID_ESP32P4) {
  return ESP_ERR_INVALID_VERSION;
 }
 if (d->magic_word != ESP_APP_DESC_MAGIC_WORD ||
     strcmp(d->project_name, KIRA_RECOVERY_EXPECTED_PROJECT) != 0) {
  return ESP_ERR_INVALID_ARG;
 }
 *size = (size_t)st.st_size;
 return ESP_OK;
}
static esp_err_t backup_kira(const esp_partition_t *p,size_t len){
 const char *tmp=KIRA_RECOVERY_DIR "/backup.tmp";FILE *f=fopen(tmp,"wb");if(!f)return ESP_FAIL;esp_err_t e=ESP_OK;
 for(size_t o=0;o<len&&e==ESP_OK;o+=IO_CHUNK){size_t n=(len-o<IO_CHUNK)?len-o:IO_CHUNK;e=esp_partition_read(p,o,s_buf,n);if(e==ESP_OK&&fwrite(s_buf,1,n,f)!=n)e=ESP_FAIL;}
 if (fclose(f) != 0 && e == ESP_OK) {
  e = ESP_FAIL;
 }
 if (e != ESP_OK) {
  unlink(tmp);
  return e;
 }
 unlink(KIRA_RECOVERY_BACKUP_FILE);
 if (rename(tmp, KIRA_RECOVERY_BACKUP_FILE) != 0) {
  return ESP_FAIL;
 }
 return ESP_OK;
}
static esp_err_t install_file(const esp_partition_t *p,const char *path){
 esp_app_desc_t d;size_t size=0;esp_err_t e=read_file_desc(path,&d,&size);if(e!=ESP_OK)return e;if(size>p->size)return ESP_ERR_INVALID_SIZE;
 FILE *f=fopen(path,"rb");if(!f)return ESP_ERR_NOT_FOUND;esp_ota_handle_t ota=0;e=esp_ota_begin(p,size,&ota);if(e!=ESP_OK){fclose(f);return e;}
 size_t done=0;while(e==ESP_OK&&done<size){size_t n=fread(s_buf,1,IO_CHUNK,f);if(!n){e=ESP_FAIL;break;}e=esp_ota_write(ota,s_buf,n);done+=n;}fclose(f);
 if(e!=ESP_OK){esp_ota_abort(ota);return e;}e=esp_ota_end(ota);if(e==ESP_OK)e=esp_ota_set_boot_partition(p);return e;
}
static void __attribute__((noreturn)) restore_or_stay(const esp_partition_t *k,bool sd,const char *why){ESP_LOGW(TAG,"%s",why);if(!sd||!file_exists(KIRA_RECOVERY_BACKUP_FILE))stay("no backup available");esp_err_t e=install_file(k,KIRA_RECOVERY_BACKUP_FILE);if(e!=ESP_OK)stay("backup restore failed");reboot("backup restored");}
static void __attribute__((noreturn)) boot_kira(const esp_partition_t *k){if(esp_ota_set_boot_partition(k)!=ESP_OK)stay("Kira image rejected");reboot("starting Kira");}
void app_main(void){
 ESP_LOGI(TAG,"Kira recovery");esp_err_t e=nvs_flash_init();if(e!=ESP_OK)ESP_LOGW(TAG,"NVS unavailable: %s",esp_err_to_name(e));uint8_t action=take_action();
 const esp_partition_t *k=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_0,NULL);if(!k)stay("partition table has no ota_0");
 esp_ota_img_states_t state=ESP_OTA_IMG_UNDEFINED;bool hs=esp_ota_get_state_partition(k,&state)==ESP_OK;bool rejected=hs&&(state==ESP_OTA_IMG_INVALID||state==ESP_OTA_IMG_ABORTED);bool trusted=!hs||state==ESP_OTA_IMG_VALID||state==ESP_OTA_IMG_UNDEFINED;
 esp_app_desc_t cur;size_t len=partition_image_len(k,&cur);bool sd=mount_sd();
 switch(action){
 case KIRA_RECOVERY_ACTION_INSTALL:
  if(!sd||!file_exists(KIRA_RECOVERY_UPDATE_FILE))stay("update requested but missing");
  if(len&&trusted&&!rejected){if(backup_kira(k,len)!=ESP_OK)stay("backup failed; refusing overwrite");}
  e=install_file(k,KIRA_RECOVERY_UPDATE_FILE);if(e==ESP_OK){unlink(KIRA_RECOVERY_UPDATE_FILE ".installed");rename(KIRA_RECOVERY_UPDATE_FILE,KIRA_RECOVERY_UPDATE_FILE ".installed");reboot("update installed");}
  restore_or_stay(k,sd,"update failed");
 case KIRA_RECOVERY_ACTION_RESTORE:restore_or_stay(k,sd,"restore requested");
 case KIRA_RECOVERY_ACTION_CRASH_LOOP:{esp_app_desc_t b;size_t bs=0;if(sd&&read_file_desc(KIRA_RECOVERY_BACKUP_FILE,&b,&bs)==ESP_OK&&(!len||memcmp(b.app_elf_sha256,cur.app_elf_sha256,sizeof(b.app_elf_sha256))!=0))restore_or_stay(k,sd,"crash loop");stay("no different backup");}
 case KIRA_RECOVERY_ACTION_STAY:stay("recovery requested");
 default:if(len&&!rejected)boot_kira(k);restore_or_stay(k,sd,rejected?"last update rolled back":"no valid Kira");
 }
}
