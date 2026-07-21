# main/CMakeLists.txt
idf_component_register(
    SRCS
        "main.cpp"
        "src/hub_app.cpp"
        "src/hub_nvs.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        common
        nvs_core
        espnow_manager
        wifi_manager
        ota_manager
        idf_hals
)
