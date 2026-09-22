file(GLOB_RECURSE SRCS "${CMAKE_CURRENT_LIST_DIR}/*.cpp" "${CMAKE_CURRENT_LIST_DIR}/*.c")

idf_component_register(SRCS ${SRCS} INCLUDE_DIRS "." REQUIRES kira_surface)

get_property(kira_runtime_stage_targets GLOBAL PROPERTY BROOKESIA_RUNTIME_APP_STAGE_TARGETS)
get_property(kira_system_stage_targets GLOBAL PROPERTY BROOKESIA_SYSTEM_SUPER_STAGE_TARGETS)
set(kira_stage_targets ${kira_runtime_stage_targets} ${kira_system_stage_targets})
if(kira_stage_targets)
    list(REMOVE_DUPLICATES kira_stage_targets)
endif()

if(COMMAND littlefs_create_partition_image)
    littlefs_create_partition_image(
        littlefs_data
        ../littlefs
        FLASH_IN_PROJECT
        DEPENDS ${kira_stage_targets}
    )
endif()
