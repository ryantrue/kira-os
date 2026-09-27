cmake_minimum_required(VERSION 3.16)
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults" "${CMAKE_CURRENT_LIST_DIR}/../sdkconfig.defaults.rev1_3")
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../version.txt" KIRA_VERSION LIMIT_COUNT 1)
execute_process(
    COMMAND git rev-parse --short=7 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    OUTPUT_VARIABLE KIRA_GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(KIRA_GIT_SHA)
    set(PROJECT_VER "${KIRA_VERSION}+${KIRA_GIT_SHA}")
else()
    set(PROJECT_VER "${KIRA_VERSION}")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(kira_recovery)
