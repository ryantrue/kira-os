idf_component_register(
    SRCS
        "src/ai_providers.cpp"
        "src/home_assistant.cpp"
        "src/hardware.cpp"
        "src/http_util.cpp"
        "src/log_share.cpp"
        "src/logger.cpp"
        "src/sd_host_shim.c"
        "src/sdcard.cpp"
        "src/semver.cpp"
        "src/settings.cpp"
        "src/updater.cpp"
        "src/version.cpp"
        "src/worker.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_common
    PRIV_REQUIRES
        app_update
        esp_app_format
        esp_driver_gpio
        esp_driver_sdmmc
        esp_http_client
        esp_http_server
        esp_ringbuf
        esp_netif
        esp_timer
        esp_wifi
        fatfs
        kira_boot
        mbedtls
        nvs_flash
        sdmmc
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
# SD card (slot 0) and ESP-Hosted (slot 1) share one SDMMC controller; see src/sd_host_shim.c.
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=sdmmc_host_init"
    "-Wl,--wrap=sdmmc_host_deinit_slot"
    "-Wl,--wrap=sdmmc_host_deinit"
)

# Consume the same local board description as Board Manager. The final IDF
# config supplies ESP-Hosted transport pin defaults after dependency resolution.
idf_build_get_property(kira_python PYTHON)
set(kira_board_dir "${PROJECT_DIR}/boards/waveshare/esp32_p4_wifi6_touch_lcd_4b")
set(kira_board_header "${CMAKE_CURRENT_BINARY_DIR}/generated/kira_board_metadata.hpp")
add_custom_command(
    OUTPUT "${kira_board_header}"
    COMMAND "${kira_python}" "${PROJECT_DIR}/tools/kira_board_metadata.py"
        "${kira_board_dir}" "${CMAKE_BINARY_DIR}/config/sdkconfig.h" "${kira_board_header}"
    DEPENDS "${PROJECT_DIR}/tools/kira_board_metadata.py"
        "${kira_board_dir}/board_devices.yaml" "${kira_board_dir}/board_peripherals.yaml"
        "${kira_board_dir}/setup_device.c" "${CMAKE_BINARY_DIR}/config/sdkconfig.h"
    VERBATIM
)
add_custom_target(kira_board_metadata DEPENDS "${kira_board_header}")
add_dependencies(${COMPONENT_LIB} kira_board_metadata)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
