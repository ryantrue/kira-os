/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "dev_display_lcd.h"
#include "esp_lcd_st7703.h"
#include "esp_lcd_touch_gt911.h"

static const char *TAG = "kira_ws4b_hal";

__attribute__((weak)) esp_err_t lcd_dsi_panel_factory_entry_t(
    esp_lcd_dsi_bus_handle_t dsi_handle,
    dev_display_lcd_config_t *lcd_cfg,
    dev_display_lcd_handles_t *lcd_handles)
{
    gpio_config_t backlight_enable = {
        .pin_bit_mask = 1ULL << GPIO_NUM_33,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&backlight_enable), TAG, "Backlight enable GPIO config failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_33, 1), TAG, "Backlight enable failed");

    st7703_vendor_config_t vendor_config = {
        .flags = {.use_mipi_interface = 1},
        .mipi_config = {
            .dsi_bus = dsi_handle,
            .dpi_config = &lcd_cfg->sub_cfg.dsi.dpi_config,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = lcd_cfg->sub_cfg.dsi.reset_gpio_num,
        .rgb_ele_order = lcd_cfg->rgb_ele_order,
        .data_endian = lcd_cfg->data_endian,
        .bits_per_pixel = lcd_cfg->bits_per_pixel,
        .flags = {.reset_active_high = lcd_cfg->sub_cfg.dsi.reset_active_high},
        .vendor_config = &vendor_config,
    };

    ESP_LOGI(TAG, "Install ST7703 720x720 DSI panel");
    return esp_lcd_new_panel_st7703(
        lcd_handles->io_handle, &panel_config, &lcd_handles->panel_handle
    );
}

__attribute__((weak)) esp_err_t lcd_touch_factory_entry_t(
    esp_lcd_panel_io_handle_t io,
    const esp_lcd_touch_config_t *config,
    esp_lcd_touch_handle_t *touch)
{
    return esp_lcd_touch_new_i2c_gt911(io, config, touch);
}
