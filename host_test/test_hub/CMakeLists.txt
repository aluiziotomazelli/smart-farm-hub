cmake_minimum_required(VERSION 3.16)

list(APPEND EXTRA_COMPONENT_DIRS
    "../../components/smart-farm-common"
    "../../components/espnow_manager"
    "../../components/wifi_manager"
    "../../components/idf_hals"
    "../../components/ota_manager"
    "../../components/time_manager"
    "../../components/ui_inputs"
    "../../components/display_gfx"
    "../gtest"
    
    "$ENV{IDF_PATH}/tools/mocks/driver"
    "$ENV{IDF_PATH}/tools/mocks/esp_wifi"
    "$ENV{IDF_PATH}/tools/mocks/esp_netif"
    "$ENV{IDF_PATH}/tools/mocks/esp_event"
    "$ENV{IDF_PATH}/tools/mocks/lwip"
)

set(COMPONENTS 
    "main"
    "smart-farm-common"
    "espnow_manager"
    "idf_hals"
    "ota_manager"
    "time_manager"
    "wifi_manager"
    "ui_inputs"
    "display_gfx"
    "esp_timer"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(test_hub)

set(EXTRACT_PATTERNS_test_hub 
    "*/main/src/*"
)

set(REMOVE_PATTERNS_test_hub 
    "*/main/include/*"
    "*host_test/*"
    "*build/*"
)

include("../coverage_common.cmake")
setup_gtest_coverage(test_hub test_hub.elf)
