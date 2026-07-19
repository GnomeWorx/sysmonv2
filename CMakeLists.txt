cmake_minimum_required(VERSION 3.12)
project(sysmonv2 VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets Network)

set(SOURCES
    main.cpp
    SystemMonitorV2.cpp
    SteamGauge.cpp
    netmon_parser.cpp
)

set(HEADERS
    SystemMonitorV2.h
    SteamGauge.h
    netmon_parser.h
)

add_executable(sysmonv2 ${SOURCES} ${HEADERS})
target_link_libraries(sysmonv2 PRIVATE Qt5::Widgets Qt5::Network)

# Version & Copyright
set(APP_COPYRIGHT "(c) GnomeWorx 2026")
configure_file(Version.h.in ${CMAKE_CURRENT_BINARY_DIR}/Version.h)
target_include_directories(sysmonv2 PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# ── Tests ──
enable_testing()
add_subdirectory(tests)
