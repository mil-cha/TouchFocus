cmake_minimum_required(VERSION 3.13)
project(indi_touchfocus_focuser VERSION 0.3.0 LANGUAGES CXX)

include(GNUInstallDirs)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(INDI QUIET IMPORTED_TARGET libindi)
endif()

# libindidriver supplies INDI::Focuser and the driver process entry point.
# StellarMate's libindi.pc exposes headers/dependencies but does not reliably
# include this library in its linker flags.
find_library(INDI_DRIVER_LIBRARY NAMES indidriver REQUIRED)

# StellarMate's Debian libindi-dev package installs libindi.pc but no
# INDIConfig.cmake. Keep a direct fallback for installations without pkg-config.
if(NOT TARGET PkgConfig::INDI)
    find_path(INDI_INCLUDE_DIR indifocuser.h PATH_SUFFIXES libindi REQUIRED)
endif()

add_executable(indi_touchfocus_focuser
    indi_touchfocus_focuser.cpp
)
target_compile_features(indi_touchfocus_focuser PRIVATE cxx_std_17)
target_compile_options(indi_touchfocus_focuser PRIVATE -Wall -Wextra -Wpedantic)
if(TARGET PkgConfig::INDI)
    target_link_libraries(indi_touchfocus_focuser PRIVATE
        PkgConfig::INDI
        ${INDI_DRIVER_LIBRARY}
    )
else()
    target_include_directories(indi_touchfocus_focuser PRIVATE ${INDI_INCLUDE_DIR})
    target_link_libraries(indi_touchfocus_focuser PRIVATE ${INDI_DRIVER_LIBRARY})
endif()

install(TARGETS indi_touchfocus_focuser RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES touchfocus_focuser.xml
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/indi
    RENAME indi_touchfocus_focuser.xml
)
install(FILES README.md
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/touchfocus-indi-driver
)
install(FILES LICENSE
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/touchfocus-indi-driver
    RENAME copyright
)

# Build a native Debian package on Raspberry Pi/StellarMate with `cpack -G DEB`.
# Architecture is detected by dpkg, so the same recipe remains usable on arm64
# and other Debian architectures supported by INDI.
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "touchfocus-indi-driver")
set(CPACK_PACKAGE_VENDOR "TouchFocus")
set(CPACK_PACKAGE_CONTACT "mil-cha <mil-cha@users.noreply.github.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "INDI focuser driver for the TouchFocus focuserd daemon")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_DEBIAN_PACKAGE_SECTION "science")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_RELEASE "1")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libindi1 (>= 2.1.7)")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
include(CPack)
