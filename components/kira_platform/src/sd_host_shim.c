/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Share the ESP32-P4's single SDMMC controller between the microSD card
 * (slot 0) and the ESP32-C6 Wi-Fi coprocessor over ESP-Hosted (slot 1).
 *
 * ESP-IDF 6.0's legacy sdmmc_host_init() creates the controller
 * unconditionally; the controller can be claimed only once. ESP-Hosted calls
 * it first, from a constructor that runs before app_main, so every later SD
 * card mount failed with
 *   SD_HOST: sd_host_create_sdmmc_controller: no available sd host controller
 *   vfs_fat_sdmmc: host init failed (0x105)
 * (seen on this board, boot log of b6ffb8f). Upstream:
 * espressif/esp-idf#17889, espressif/esp-hosted-mcu#124.
 *
 * Teardown has the same single-owner assumption and is worse: a failed or
 * unmounted SD card calls sdmmc_host_deinit_slot(0), which also deletes the
 * shared controller while slot 1 (the C6) is still using it; the next SDIO
 * interrupt then runs on a dangling pointer and panics.
 *
 * The rest of the driver already supports a shared controller, so the fix is
 * a guard: later sdmmc_host_init() calls succeed on the existing controller,
 * and the controller is never torn down (slot 0 is re-added over the existing
 * handle on the next mount). Wrapped with -Wl,--wrap (kira_platform
 * CMakeLists.txt) so managed components stay untouched.
 *
 * Same fix as github.com/SomersetEV/ESP32P47inchscreen (main/sd_host_init_shim.c)
 * on the Waveshare ESP32-P4-WIFI6-Touch-LCD-7B. Remove when ESP-IDF guards
 * the shared controller itself. Recovery does not use ESP-Hosted and does not
 * need this.
 */
#include <stdbool.h>

#include "esp_err.h"
#include "esp_log.h"

esp_err_t __real_sdmmc_host_init(void);

static const char *TAG = "kira_sdhost";
static bool s_initialised;

esp_err_t __wrap_sdmmc_host_init(void)
{
    if (s_initialised) {
        return ESP_OK;  // shared controller already up for the other slot
    }
    esp_err_t err = __real_sdmmc_host_init();
    if (err == ESP_OK) {
        s_initialised = true;
    } else {
        ESP_LOGE(TAG, "sdmmc_host_init failed: %s", esp_err_to_name(err));
    }
    return err;
}

// Never delete the shared controller: ESP-Hosted keeps using slot 1.
esp_err_t __wrap_sdmmc_host_deinit_slot(int slot)
{
    (void)slot;
    return ESP_OK;
}

// Would remove both slots, including the Wi-Fi coprocessor's.
esp_err_t __wrap_sdmmc_host_deinit(void)
{
    return ESP_OK;
}
