idf_component_register(
    SRCS 
        "main.cpp"
        "test_message_dispatcher.cpp"
        "../../../main/src/message_dispatcher.cpp"
        "../../../main/src/handlers/water_tank_handler.cpp"
        "../../../main/src/handlers/ota_status_handler.cpp"
    INCLUDE_DIRS 
        "."
        "../../../main/include"
        "../../../main"
        "../../../components/idf_hals/mocks"
    REQUIRES 
        gtest
        smart-farm-common
        espnow_manager
        wifi_manager
        idf_hals
        ota_manager
        time_manager
        WHOLE_ARCHIVE
)

if(IDF_TARGET STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PROJ_HOST_TEST)
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()
