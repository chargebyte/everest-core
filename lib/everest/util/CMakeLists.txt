add_library(everest_util INTERFACE)
add_library(everest::util ALIAS everest_util)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set_target_properties(everest_util PROPERTIES
  VERSION 0.0.1
)


target_include_directories(everest_util
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

set_target_properties(everest_util
    PROPERTIES
        BUILD_INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include"
        EXPORT_NAME util
)

if (NOT DISABLE_EDM)
    # everest_util also gets its own standalone, find_package-able config
    # below when DISABLE_EDM is set (see comment there). Since it's a
    # header-only INTERFACE library, participating in EXPORT
    # everest-core-targets at the same time would make CMake see
    # everest::util defined twice by the time everest-core-config.cmake's
    # own combined everest-core-targets.cmake gets included -- "Some (but
    # not all) targets in this export set were already defined." So only
    # join the combined export set in EDM mode, where the standalone
    # package below doesn't exist.
    install(TARGETS everest_util
        EXPORT everest-core-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/everest
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp"
)

if(DISABLE_EDM)
    # Do not use evc_setup_package() here: it unconditionally installs an
    # export file for the package, which would export everest_util a second
    # time alongside everest-core-targets and break non-EDM consumers.
    set(EVEREST_UTIL_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/everest-util")

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/everest-util-config.cmake
        INSTALL_DESTINATION ${EVEREST_UTIL_CMAKE_INSTALL_DIR}
        PATH_VARS CMAKE_INSTALL_INCLUDEDIR
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/everest-util-config-version.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY ExactVersion
    )

    install(
        FILES
            ${CMAKE_CURRENT_BINARY_DIR}/everest-util-config.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/everest-util-config-version.cmake
        DESTINATION ${EVEREST_UTIL_CMAKE_INSTALL_DIR}
    )
endif()

if (BUILD_TESTING)
    add_subdirectory(tests)
endif()
