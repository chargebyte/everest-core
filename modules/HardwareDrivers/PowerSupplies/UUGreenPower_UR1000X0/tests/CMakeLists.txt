set(TEST_TARGET_NAME ${PROJECT_NAME}_uugreen_tests)
add_executable(${TEST_TARGET_NAME}
    test_uugreen.cpp
    ../can_driver_acdc/CanDevice.cpp
    ../can_driver_acdc/CanPackets.cpp
    ../can_driver_acdc/UUGreenCanDevice.cpp
)
target_include_directories(${TEST_TARGET_NAME} PRIVATE ../can_driver_acdc)
target_link_libraries(${TEST_TARGET_NAME} PRIVATE GTest::gtest_main Pal::Sigslot everest::log)
# Observe transmitted frames without requiring a privileged virtual CAN interface.
target_link_options(${TEST_TARGET_NAME} PRIVATE "-Wl,--wrap=_ZN9CanDevice3_txEjRKSt6vectorIhSaIhEE")
add_test(NAME ${TEST_TARGET_NAME} COMMAND ${TEST_TARGET_NAME})
ev_register_test_target(${TEST_TARGET_NAME})
