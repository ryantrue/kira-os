file(GLOB_RECURSE SRCS "${CMAKE_CURRENT_LIST_DIR}/*.cpp" "${CMAKE_CURRENT_LIST_DIR}/*.c")

idf_component_register(SRCS ${SRCS} INCLUDE_DIRS "." REQUIRES kira_app kira_ha_app kira_recents kira_boot kira_platform kira_settings kira_audio)

get_property(kira_runtime_stage_targets GLOBAL PROPERTY BROOKESIA_RUNTIME_APP_STAGE_TARGETS)
get_property(kira_system_stage_targets GLOBAL PROPERTY BROOKESIA_SYSTEM_SUPER_STAGE_TARGETS)
set(kira_stage_targets ${kira_runtime_stage_targets} ${kira_system_stage_targets})
if(kira_stage_targets)
    list(REMOVE_DUPLICATES kira_stage_targets)
endif()

# Kira overrides on the staged Brookesia resources (tools/kira_resource_overrides.py),
# applied after staging and before the LittleFS image is built.
get_filename_component(kira_littlefs_stage "${CMAKE_CURRENT_LIST_DIR}/../littlefs" ABSOLUTE)
add_custom_target(kira_resource_overrides
    COMMAND ${PYTHON} "${CMAKE_CURRENT_LIST_DIR}/../tools/kira_resource_overrides.py" "${kira_littlefs_stage}"
    COMMENT "Applying Kira resource overrides"
    VERBATIM
)
if(kira_stage_targets)
    add_dependencies(kira_resource_overrides ${kira_stage_targets})
endif()

if(COMMAND littlefs_create_partition_image)
    littlefs_create_partition_image(
        littlefs_data
        ../littlefs
        FLASH_IN_PROJECT
        DEPENDS ${kira_stage_targets} kira_resource_overrides
    )
endif()
