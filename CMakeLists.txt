cmake_minimum_required(VERSION 3.16)

if("${CMAKE_GENERATOR}" MATCHES "Ninja")
    set(CMAKE_NINJA_FORCE_RESPONSE_FILE 1 CACHE INTERNAL "")
endif()

option(CCACHE_ENABLE "Use ccache" ON)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(kira_os)

idf_build_set_property(COMPILE_OPTIONS -Wno-error=stringop-truncation APPEND)
brookesia_system_super_apply_compile_policy()
