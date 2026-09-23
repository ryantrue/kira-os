idf_component_register(
    SRCS "boot.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES app_update esp_partition esp_timer nvs_flash)
