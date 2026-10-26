set(TEST_TARGET_NAME ${PROJECT_NAME}_ast_dc650_tests)
add_executable(${TEST_TARGET_NAME}
    test_ast_dc650.cpp
    ../ast_app_layer.cpp
    ../slip_protocol.cpp
    ../serial_device.cpp
    ../transport.cpp
    ../meter.cpp
    ../main/powermeterImpl.cpp
    ../temperature/temperature_sensorImpl.cpp
    ../diagnostics.cpp
)
get_target_property(GENERATED_INCLUDE_DIR generate_cpp_files EVEREST_GENERATED_INCLUDE_DIR)
target_include_directories(${TEST_TARGET_NAME} PRIVATE .. ${GENERATED_INCLUDE_DIR} ${CMAKE_BINARY_DIR}/generated/modules/AST_DC650)
target_link_libraries(${TEST_TARGET_NAME} PRIVATE GTest::gtest_main everest::framework everest::crc)
target_compile_features(${TEST_TARGET_NAME} PRIVATE cxx_std_17)
add_dependencies(${TEST_TARGET_NAME} AST_DC650)
add_test(NAME ${TEST_TARGET_NAME} COMMAND ${TEST_TARGET_NAME})
ev_register_test_target(${TEST_TARGET_NAME})
