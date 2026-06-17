cmake_minimum_required(VERSION 3.21)

project(cpp-rcp
    VERSION 0.3.0
    DESCRIPTION "Remote Control Protocol for automotive zonal architecture (C++)"
    HOMEPAGE_URL "https://github.com/SoundMatt/cpp-RCP"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wshadow -Wnon-virtual-dtor
        -Wold-style-cast -Wcast-align
        -Wunused -Woverloaded-virtual
        -Wno-unused-parameter
    )
endif()
if(MSVC)
    add_compile_options(
        /W4 /WX
        /wd4100   # unreferenced formal parameter
        /wd4996
    )
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(cmake/FetchDeps.cmake)

# ── Core library (header-only interface target) ───────────────────────────────
add_library(rcp INTERFACE)
add_library(rcp::rcp ALIAS rcp)

target_include_directories(rcp INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(rcp INTERFACE cxx_std_17)

find_package(Threads REQUIRED)
target_link_libraries(rcp INTERFACE Threads::Threads)

# ── Tests ─────────────────────────────────────────────────────────────────────
option(RCP_BUILD_TESTS "Build tests" ON)
if(RCP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── RELAY-conformant CLI (cpp-rcp) ────────────────────────────────────────────
option(RELAY_BUILD_CLI "Build the cpp-rcp RELAY-conformant CLI (spec §11)" OFF)
if(RELAY_BUILD_CLI)
    add_executable(cpp-rcp cli/main.cpp)
    target_link_libraries(cpp-rcp PRIVATE rcp::rcp)
    set_target_properties(cpp-rcp PROPERTIES OUTPUT_NAME cpp-rcp)
endif()

# ── Install ───────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS rcp EXPORT rcpTargets)
install(EXPORT rcpTargets
    NAMESPACE rcp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rcp
)

message(STATUS "cpp-rcp v${PROJECT_VERSION} — C++${CMAKE_CXX_STANDARD} / ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
