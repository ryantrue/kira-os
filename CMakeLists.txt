cmake_minimum_required(VERSION 3.16)

if("${CMAKE_GENERATOR}" MATCHES "Ninja")
    set(CMAKE_NINJA_FORCE_RESPONSE_FILE 1 CACHE INTERNAL "")
endif()

option(CCACHE_ENABLE "Use ccache" ON)

# Kira owns the defaults order here. Before Board Manager has generated its
# layer, keep board-specific defaults out of kconfig: those defaults reference
# conditional Kconfig symbols that do not exist until the component graph has
# expanded. After gen-bmgr-config, the generated marker exists and the complete
# product profile is applied deterministically.
set(KIRA_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(KIRA_BOARD_DIR "${KIRA_ROOT}/boards/waveshare/esp32_p4_wifi6_touch_lcd_4b")
set(_kira_base_defaults
    "${KIRA_ROOT}/sdkconfig.defaults"
    "${KIRA_ROOT}/sdkconfig.defaults.kira")
set(_kira_rev_defaults "${KIRA_ROOT}/sdkconfig.defaults.rev1_3")
set(_kira_board_defaults "${KIRA_BOARD_DIR}/sdkconfig.defaults.board")
set(_kira_bmgr_defaults "${KIRA_ROOT}/components/gen_bmgr_codes/board_manager.defaults")

foreach(_f IN LISTS _kira_base_defaults)
    if(NOT EXISTS "${_f}")
        message(FATAL_ERROR "Kira: required sdkconfig defaults layer is missing: ${_f}")
    endif()
endforeach()
if(NOT EXISTS "${_kira_rev_defaults}" OR NOT EXISTS "${_kira_board_defaults}")
    message(FATAL_ERROR "Kira: board/revision defaults are missing")
endif()

set(_kira_defaults ${_kira_base_defaults})
if(EXISTS "${_kira_bmgr_defaults}")
    list(APPEND _kira_defaults "${_kira_bmgr_defaults}" "${_kira_board_defaults}")
endif()
# Silicon profile is always last.
list(APPEND _kira_defaults "${_kira_rev_defaults}")
set(SDKCONFIG_DEFAULTS "${_kira_defaults}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(COMPILE_OPTIONS -Wno-error=stringop-truncation APPEND)
idf_build_set_property(CXX_COMPILE_OPTIONS -Wno-error=attributes APPEND)

project(kira_os)
brookesia_system_super_apply_compile_policy()
