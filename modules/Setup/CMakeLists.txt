#
# AUTO GENERATED - MARKED REGIONS WILL BE KEPT
# template version 3
#

# module setup:
#   - ${MODULE_NAME}: module name
ev_setup_cpp_module()

# ev@bcc62523-e22b-41d7-ba2f-825b493a3c97:v1
# insert your custom targets and additional config variables here
target_sources(${MODULE_NAME}
    PRIVATE
        "WiFiSetup.cpp"
)

target_link_libraries(${MODULE_NAME}
    PRIVATE
        everest::staging::run_application
)
# ev@bcc62523-e22b-41d7-ba2f-825b493a3c97:v1

target_sources(${MODULE_NAME}
    PRIVATE
        "main/emptyImpl.cpp"
)

# ev@c55432ab-152c-45a9-9d2e-7281d50c69c3:v1
target_compile_features(${MODULE_NAME} PRIVATE cxx_std_17)
if(EVEREST_CORE_BUILD_TESTING)
    include(CTest)
    add_subdirectory(tests)
    set(CMAKE_BUILD_TYPE Debug)
endif()
# ev@c55432ab-152c-45a9-9d2e-7281d50c69c3:v1
