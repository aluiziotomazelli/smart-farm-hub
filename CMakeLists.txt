# CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

set(PROJECT_NAME "hub")

list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/smart-farm-common/common"
    "${CMAKE_CURRENT_LIST_DIR}/components/smart-farm-common/nvs_core"
    "${CMAKE_CURRENT_LIST_DIR}/components/espnow_manager"
    "${CMAKE_CURRENT_LIST_DIR}/components/wifi_manager"
    "${CMAKE_CURRENT_LIST_DIR}/components/ota_manager"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(${PROJECT_NAME})
