cmake_minimum_required(VERSION 3.22)
project(edgeline-agent VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# The UI is Electron now, so nothing here links Qt Widgets or Gui. Keeping
# Core and Network only is what lets the package drop libqt6widgets6.
find_package(Qt6 REQUIRED COMPONENTS Core Network)
find_package(PkgConfig REQUIRED)

# Strict warnings for our own code (kept warning-clean; not applied to Qt moc).
add_compile_options(-Wall -Wextra -Wpedantic)

# Single source of truth for the version. Overridable so a packager can stamp a
# build, and so the update check can be exercised against a real release
# without editing the project version.
set(EDGELINE_VERSION_STRING "${PROJECT_VERSION}" CACHE STRING
    "Version string this build reports")
add_compile_definitions(EDGELINE_VERSION="${EDGELINE_VERSION_STRING}")

# Releases carry a codename, as the design's Updates card shows. Kept beside the
# version so the two cannot drift.
set(EDGELINE_CODENAME "Cold Start" CACHE STRING "Codename for this release")
add_compile_definitions(EDGELINE_CODENAME="${EDGELINE_CODENAME}")

# Where the update check looks. A fork should point this at its own repo rather
# than silently telling its users to install someone else's releases.
set(EDGELINE_UPDATE_REPO "aabdelghani/corsair-xeneon-edge-linux" CACHE STRING
    "GitHub owner/repo the update check queries")
add_compile_definitions(EDGELINE_UPDATE_REPO="${EDGELINE_UPDATE_REPO}")

pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-hidraw)
pkg_check_modules(X11XI REQUIRED IMPORTED_TARGET x11 xi)

# --- proto: pure protocol framing, no deps, unit-testable ---
add_library(edgeline_proto STATIC
    src/proto/BragiFrame.cpp
)
target_include_directories(edgeline_proto PUBLIC src)

# --- pure logic: no Qt, no X11, no processes. The parts worth testing hardest.
add_library(edgeline_logic STATIC
    src/core/Calibration.cpp
    src/core/DdcCapabilities.cpp
)
target_include_directories(edgeline_logic PUBLIC src)

# --- transport: hidapi-hidraw only ---
add_library(edgeline_transport STATIC
    src/transport/HidEnumerator.cpp
    src/transport/HidRecon.cpp
    src/transport/HidTransport.cpp
)
target_link_libraries(edgeline_transport PUBLIC edgeline_proto PkgConfig::HIDAPI)

# --- core: device logic behind a Qt event loop ---
add_library(edgeline_core STATIC
    src/core/AppSettings.cpp
    src/core/DdcClient.cpp
    src/core/EdgeDevice.cpp
    src/core/Profiles.cpp
    src/core/SensorSource.cpp
    src/core/TouchControl.cpp
    src/core/UpdateChecker.cpp
    src/core/WriteGate.cpp
    src/ipc/RpcClient.cpp
    src/ipc/RpcServer.cpp
    src/x11/TouchEventSource.cpp
    src/x11/TouchProbe.cpp
)
target_link_libraries(edgeline_core
    PUBLIC edgeline_transport edgeline_logic Qt6::Core Qt6::Network PkgConfig::X11XI)

# --- the headless agent: owns every device, speaks JSON on a socket ---
add_executable(edgeline-agent
    src/agent/main.cpp
    src/agent/Api.cpp
)
target_link_libraries(edgeline-agent PRIVATE edgeline_core)

# --- the CLI ---
add_executable(edgeline src/cli/main.cpp)
target_link_libraries(edgeline PRIVATE edgeline_core)

# --- tests ---
enable_testing()

add_executable(edgeline_proto_tests tests/test_proto.cpp)
target_link_libraries(edgeline_proto_tests PRIVATE edgeline_proto)
target_include_directories(edgeline_proto_tests PRIVATE tests)
add_test(NAME proto COMMAND edgeline_proto_tests)

add_executable(edgeline_version_tests tests/test_version.cpp)
target_link_libraries(edgeline_version_tests PRIVATE edgeline_core)
target_include_directories(edgeline_version_tests PRIVATE tests)
add_test(NAME version COMMAND edgeline_version_tests)

add_executable(edgeline_calibration_tests tests/test_calibration.cpp)
target_link_libraries(edgeline_calibration_tests PRIVATE edgeline_logic)
target_include_directories(edgeline_calibration_tests PRIVATE tests)
add_test(NAME calibration COMMAND edgeline_calibration_tests)

add_executable(edgeline_capabilities_tests tests/test_capabilities.cpp)
target_link_libraries(edgeline_capabilities_tests PRIVATE edgeline_logic)
target_include_directories(edgeline_capabilities_tests PRIVATE tests)
# Reads a capability string captured from the real panel.
add_test(NAME capabilities
         COMMAND edgeline_capabilities_tests
                 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/xeneon-edge-capabilities.txt)
