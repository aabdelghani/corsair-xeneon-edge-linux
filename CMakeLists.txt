cmake_minimum_required(VERSION 3.22)
project(xeneon-ctl VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(PkgConfig REQUIRED)
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
    src/core/WriteGate.cpp
    src/x11/TouchEventSource.cpp
)
target_link_libraries(xeneon_core PUBLIC xeneon_transport Qt6::Core PkgConfig::X11XI)

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
add_test(NAME proto COMMAND xeneon_tests)
