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

# everest_util also gets its own standalone, find_package-able config below
# when DISABLE_EDM is set (see comment there), needed so consumers like
# ocpp can resolve everest::util before everest-core-config.cmake reaches
# its own combined include(everest-core-targets.cmake) several
# find_package() calls later. Reusing the SAME "everest-core-targets"
# export set for both would make CMake see everest::util defined once via
# the standalone package, then fail when everest-core-targets.cmake tries
# to define it again as part of the combined set ("Some (but not all)
# targets in this export set were already defined."). But everest_util
# still needs to be in *some* named export set, or install(EXPORT ...)
# fails for everything that links it PUBLIC (everest_io, framework, ocpp,
# tls: "requires target everest_util that is not in any export set"). So
# give it its own dedicated export set in DISABLE_EDM mode instead of
# dropping export participation entirely.
if (DISABLE_EDM)
    set(EVEREST_UTIL_EXPORT_SET util-targets)
else()
    set(EVEREST_UTIL_EXPORT_SET everest-core-targets)
endif()

install(TARGETS everest_util
    EXPORT ${EVEREST_UTIL_EXPORT_SET}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/everest
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp"
)

if(DISABLE_EDM)
    # Do not use evc_setup_package() here: it unconditionally installs an
    # export file using the EXPORT set's own default name, and we need
    # "util-targets.cmake" to match what config.cmake.in includes below.
    set(EVEREST_UTIL_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/everest-util")

    install(
        EXPORT util-targets
        FILE util-targets.cmake
        NAMESPACE everest::
        DESTINATION ${EVEREST_UTIL_CMAKE_INSTALL_DIR}
    )

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
