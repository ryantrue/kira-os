/* SPDX-License-Identifier: Apache-2.0 */
#include "kira/platform/sdcard.hpp"

#include <functional>
#include <mutex>

#include "driver/sdmmc_host.h"
#include "esp_board_manager_includes.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"

#include "kira/platform/logger.hpp"
#include "kira/platform/worker.hpp"

namespace kira::platform::sdcard {
namespace {

constexpr const char *TAG = "kira_sd";
constexpr const char *DEVICE_NAME = "fs_sdcard";

std::mutex s_mutex;
Status s_status;

void publish(const std::function<void(Status &)> &update)
{
    std::lock_guard lock(s_mutex);
    update(s_status);
    ++s_status.generation;
}

void refresh_now(const char *message)
{
    uint64_t total = 0;
    uint64_t free = 0;
    const bool mounted = esp_vfs_fat_info(MOUNT_POINT, &total, &free) == ESP_OK;
    publish([&](Status &s) {
        s.mounted = mounted;
        s.total_bytes = mounted ? total : 0;
        s.free_bytes = mounted ? free : 0;
        s.filesystem = mounted ? "FAT" : "";
        if (message != nullptr) {
            s.message = message;
        } else if (!mounted) {
            s.message = "No card, or the card is not FAT formatted";
        } else {
            s.message.clear();
        }
    });
}

// Card mounted by Board Manager: format in place, the volume stays mounted.
esp_err_t format_mounted(sdmmc_card_t *card)
{
    ESP_LOGW(TAG, "formatting mounted card at %s", MOUNT_POINT);
    return esp_vfs_fat_sdcard_format(MOUNT_POINT, card);
}

// Card that Board Manager could not mount (no FAT filesystem, e.g. exFAT):
// bring the bus up with the board's own SDMMC settings, let FATFS create a
// filesystem, release everything and ask Board Manager to mount it normally.
esp_err_t format_unmounted()
{
    dev_fs_fat_config_t *cfg = nullptr;
    esp_err_t err = esp_board_manager_get_device_config(DEVICE_NAME, reinterpret_cast<void **>(&cfg));
    if (err != ESP_OK || cfg == nullptr) {
        ESP_LOGE(TAG, "no board configuration for %s", DEVICE_NAME);
        return err != ESP_OK ? err : ESP_ERR_NOT_FOUND;
    }
    const dev_fs_fat_sdmmc_sub_config_t &sd = cfg->sub_cfg.sdmmc;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = sd.slot;
    host.max_freq_khz = static_cast<int>(cfg->frequency);  // SDMMC_FREQ_* values are kHz

    sd_pwr_ctrl_handle_t pwr_ctrl = nullptr;
    if (sd.ldo_chan_id >= 0) {
        sd_pwr_ctrl_ldo_config_t ldo_config = {};
        ldo_config.ldo_chan_id = sd.ldo_chan_id;
        err = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SD power control: %s", esp_err_to_name(err));
            return err;
        }
        host.pwr_ctrl_handle = pwr_ctrl;
    }

    // Same slot setup as Board Manager's dev_fs_fat_sub_sdmmc.c.
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.cd = static_cast<gpio_num_t>(sd.pins.cd);
    slot.wp = static_cast<gpio_num_t>(sd.pins.wp);
    slot.clk = static_cast<gpio_num_t>(sd.pins.clk);
    slot.cmd = static_cast<gpio_num_t>(sd.pins.cmd);
    slot.d0 = static_cast<gpio_num_t>(sd.pins.d0);
    slot.d1 = static_cast<gpio_num_t>(sd.pins.d1);
    slot.d2 = static_cast<gpio_num_t>(sd.pins.d2);
    slot.d3 = static_cast<gpio_num_t>(sd.pins.d3);
    slot.d4 = static_cast<gpio_num_t>(sd.pins.d4);
    slot.d5 = static_cast<gpio_num_t>(sd.pins.d5);
    slot.d6 = static_cast<gpio_num_t>(sd.pins.d6);
    slot.d7 = static_cast<gpio_num_t>(sd.pins.d7);
    slot.width = sd.bus_width;
    slot.flags = sd.slot_flags;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = true;
    mount_config.max_files = 4;
    mount_config.allocation_unit_size = 32 * 1024;

    sdmmc_card_t *card = nullptr;
    ESP_LOGW(TAG, "mounting with format_if_mount_failed to create a FAT volume");
    err = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot, &mount_config, &card);
    if (err == ESP_OK) {
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
    } else {
        ESP_LOGE(TAG, "format failed: %s", esp_err_to_name(err));
    }
    if (pwr_ctrl != nullptr) {
        sd_pwr_ctrl_del_on_chip_ldo(pwr_ctrl);
    }
    if (err != ESP_OK) {
        return err;
    }
    err = esp_board_manager_init_device_by_name(DEVICE_NAME);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "card formatted but Board Manager mount failed: %s", esp_err_to_name(err));
    }
    return err;
}

void format_job()
{
    logger::pause_sd(true);
    esp_err_t err = ESP_OK;
    void *handle = nullptr;
    if (esp_board_manager_get_device_handle(DEVICE_NAME, &handle) == ESP_OK && handle != nullptr &&
            static_cast<dev_fs_fat_handle_t *>(handle)->card != nullptr) {
        err = format_mounted(static_cast<dev_fs_fat_handle_t *>(handle)->card);
    } else {
        err = format_unmounted();
    }
    logger::pause_sd(false);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "card formatted");
        refresh_now("Formatted as FAT. Apps see the card after a restart.");
    } else {
        refresh_now("Format failed; see the log");
    }
    publish([](Status &s) { s.busy = false; });
}

}  // namespace

bool is_mounted()
{
    uint64_t total = 0;
    uint64_t free = 0;
    return esp_vfs_fat_info(MOUNT_POINT, &total, &free) == ESP_OK;
}

Status status()
{
    std::lock_guard lock(s_mutex);
    return s_status;
}

void refresh_async()
{
    publish([](Status &s) { s.busy = true; });
    if (!submit_job("sd_refresh", [] {
            refresh_now(nullptr);
            publish([](Status &s) { s.busy = false; });
        })) {
        publish([](Status &s) { s.busy = false; s.message = "Busy, try again"; });
    }
}

void format_async()
{
    publish([](Status &s) { s.busy = true; s.message = "Formatting..."; });
    if (!submit_job("sd_format", format_job)) {
        publish([](Status &s) { s.busy = false; s.message = "Busy, try again"; });
    }
}

}  // namespace kira::platform::sdcard
