# Test harnesses for sysmonv2.
# Pure logic tests — no display server, no live /proc required.
find_package(Qt5 REQUIRED COMPONENTS Test Widgets Network)

# --- test_netmon: network-parsing QTest harness ---
add_executable(test_netmon
    test_netmon.cpp
    ../netmon_parser.cpp
)

target_link_libraries(test_netmon PRIVATE Qt5::Test Qt5::Widgets Qt5::Network)
target_include_directories(test_netmon PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

# --- test_steamgauge: SteamGauge widget tests ---
add_executable(test_steamgauge
    test_steamgauge.cpp
    ../SteamGauge.cpp
)

target_link_libraries(test_steamgauge PRIVATE Qt5::Test Qt5::Widgets)
target_include_directories(test_steamgauge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

# --- test_sysmonv2: SystemMonitorV2 UI integration tests ---
add_executable(test_sysmonv2
    test_sysmonv2_ui.cpp
    ../SystemMonitorV2.cpp
    ../SteamGauge.cpp
    ../netmon_parser.cpp
)

target_link_libraries(test_sysmonv2 PRIVATE Qt5::Test Qt5::Widgets Qt5::Network)
target_include_directories(test_sysmonv2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

# --- CTest registration ---
enable_testing()
add_test(NAME netmon_tests    COMMAND test_netmon)
add_test(NAME steamgauge_tests COMMAND test_steamgauge)
add_test(NAME sysmonv2_tests  COMMAND test_sysmonv2)
