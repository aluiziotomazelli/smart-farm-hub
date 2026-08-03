cmake_minimum_required(VERSION 3.16)

list(APPEND EXTRA_COMPONENT_DIRS
    "../../components/smart-farm-common"
    "../../components/espnow_manager"
    "../../components/wifi_manager"
    "../../components/idf_hals"
    "../../components/ota_manager"
    "../../components/time_manager"
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
    "esp_timer"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(test_message_dispatcher)

set(EXTRACT_PATTERNS_test_message_dispatcher 
    "*/main/src/*"
)

set(REMOVE_PATTERNS_test_message_dispatcher 
    "*/main/include/*"
    "*host_test/*"
    "*build/*"
)

include("../coverage_common.cmake")
setup_gtest_coverage(test_message_dispatcher test_message_dispatcher.elf)
