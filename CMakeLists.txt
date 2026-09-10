cmake_minimum_required(VERSION 3.22)
project(xeneon-ctl VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
find_package(PkgConfig REQUIRED)

# Strict warnings for our own code (kept warning-clean; not applied to Qt moc).
add_compile_options(-Wall -Wextra -Wpedantic)

# Single source of truth for the version string shown by --version and the UI.
# Overridable so a packager can stamp a build, and so the update check can be
# exercised against a real release without editing the project version.
set(XENEON_VERSION_STRING "${PROJECT_VERSION}" CACHE STRING
    "Version string this build reports")
add_compile_definitions(XENEON_VERSION="${XENEON_VERSION_STRING}")

# Where the update check looks. A fork should point this at its own repo rather
# than silently telling its users to install someone else's releases.
set(XENEON_UPDATE_REPO "aabdelghani/corsair-xeneon-edge-linux" CACHE STRING
    "GitHub owner/repo the update check queries")
add_compile_definitions(XENEON_UPDATE_REPO="${XENEON_UPDATE_REPO}")
pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-hidraw)
pkg_check_modules(X11XI REQUIRED IMPORTED_TARGET x11 xi)

# --- proto: pure protocol framing, no deps, unit-testable ---
add_library(xeneon_proto STATIC
    src/proto/BragiFrame.cpp
)
target_include_directories(xeneon_proto PUBLIC src)

# --- transport: hidapi-hidraw only ---
add_library(xeneon_transport STATIC
    src/transport/HidEnumerator.cpp
    src/transport/HidRecon.cpp
    src/transport/HidTransport.cpp
)
target_link_libraries(xeneon_transport PUBLIC xeneon_proto PkgConfig::HIDAPI)

# --- core: Qt-aware device facade ---
add_library(xeneon_core STATIC
    src/core/AppSettings.cpp
    src/core/DdcClient.cpp
    src/core/EdgeDevice.cpp
    src/core/SensorSource.cpp
    src/core/TouchControl.cpp
    src/core/UpdateChecker.cpp
    src/core/WriteGate.cpp
    src/x11/TouchEventSource.cpp
    src/x11/TouchProbe.cpp
)
target_link_libraries(xeneon_core PUBLIC xeneon_transport Qt6::Core Qt6::Network PkgConfig::X11XI)

# --- cli ---
add_executable(xeneonctl src/cli/main.cpp)
target_link_libraries(xeneonctl PRIVATE xeneon_core)

# --- gui ---
add_executable(xeneon-edge
    src/ui/main.cpp
    src/ui/MainWindow.cpp
    src/ui/CalibrationOverlay.cpp
    src/ui/TouchIndicatorOverlay.cpp
    src/ui/TouchOverlay.cpp
    src/ui/dashboard/DashboardWindow.cpp
    src/ui/pages/DevicePage.cpp
    src/ui/pages/DisplayPage.cpp
    src/ui/pages/ProbePage.cpp
    src/ui/resources.qrc
)
target_link_libraries(xeneon-edge PRIVATE xeneon_core Qt6::Widgets)

# --- tests ---
enable_testing()
add_executable(xeneon_tests tests/test_proto.cpp)
target_link_libraries(xeneon_tests PRIVATE xeneon_proto)
target_include_directories(xeneon_tests PRIVATE tests)
add_test(NAME proto COMMAND xeneon_tests)

add_executable(xeneon_version_tests tests/test_version.cpp)
target_link_libraries(xeneon_version_tests PRIVATE xeneon_core)
target_include_directories(xeneon_version_tests PRIVATE tests)
add_test(NAME version COMMAND xeneon_version_tests)

# ---------------------------------------------------------------------------
# Installation (for packaging: .deb, Flatpak, plain `make install`)
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS xeneon-edge xeneonctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES packaging/xeneon-ctl.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

install(FILES packaging/io.github.aabdelghani.xeneon-ctl.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)

install(FILES packaging/icons/xeneon-ctl.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

foreach(sz 16 24 32 48 64 128 256 512)
    install(FILES packaging/icons/xeneon-ctl-${sz}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${sz}x${sz}/apps
            RENAME xeneon-ctl.png)
endforeach()

# The udev rule grants the logged-in user access to the Edge HID interface.
install(FILES udev/60-corsair-xeneon.rules
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/udev/rules.d)

# ---------------------------------------------------------------------------
# CPack: produce a Debian package (installable like Chrome's .deb)
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "xeneon-ctl")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "Ahmed Abdelghany <ahmedabdelghany15@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Native Linux control for the Corsair Xeneon Edge touchscreen")
set(CPACK_PACKAGE_HOMEPAGE_URL
    "https://github.com/aabdelghani/corsair-xeneon-edge-linux")

set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libqt6widgets6, libqt6core6, libqt6network6, libhidapi-hidraw0, libx11-6, libxi6, ddcutil")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "x11-xserver-utils")
# Refresh the icon cache / desktop database after install/remove.
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postrm")
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
include(CPack)
