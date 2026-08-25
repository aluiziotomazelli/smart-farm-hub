idf_component_register(
    SRCS 
        "main.cpp"
        "test_message_dispatcher.cpp"
        "test_command_manager.cpp"
        "test_ui_input_manager.cpp"
        "test_ui_controller.cpp"
        "test_solar_power_estimator.cpp"
        "test_solar_sensor_handler.cpp"
        "test_load_control_handler.cpp"
        "test_energy_monitor_and_domain_controllers.cpp"
        "test_tank_controller.cpp"
        "test_load_control_engine.cpp"
        "test_ui_snapshot.cpp"
        "test_node_registry.cpp"
        "../../../main/src/command_manager.cpp"
        "../../../main/src/message_dispatcher.cpp"
        "../../../main/src/handlers/water_tank_handler.cpp"
        "../../../main/src/handlers/solar_sensor_handler.cpp"
        "../../../main/src/handlers/load_control_handler.cpp"
        "../../../main/src/handlers/ota_status_handler.cpp"
        "../../../main/src/energy_monitor.cpp"
        "../../../main/src/tank_controller.cpp"
        "../../../main/src/load_control_engine.cpp"
        "../../../main/src/node_registry.cpp"
        "../../../main/src/ui_input_manager.cpp"
        "../../../main/src/ui_controller.cpp"
    INCLUDE_DIRS 
        "."
        "../../../main/include"
        "../../../main"
        "../../../components/idf_hals/mocks"
        "../../../components/display_gfx/include"
        "../../../components/display_gfx/host_test/common"
        "../../../components/smart-farm-common/host_test/common"
        "../../../components/ui_inputs/host_test/common"
        "../../../components/espnow_manager/include/mocks"
        "../../../components/time_manager/host_test/mocks"
    REQUIRES 
        gtest
        smart-farm-common
        espnow_manager
        wifi_manager
        idf_hals
        ota_manager
        time_manager
        ui_inputs
        display_gfx
        etl
        WHOLE_ARCHIVE
)

if(IDF_TARGET STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PROJ_HOST_TEST)
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()
