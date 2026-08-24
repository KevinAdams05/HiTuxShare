cmake_minimum_required(VERSION 3.16)

project(HiTuxShare
    VERSION 0.1.0
    DESCRIPTION "Native Linux BeShare/MUSCLE file-sharing and chat client"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(HITUX_BUILD_GUI "Build the Qt 6 GUI front-end" ON)
option(HITUX_BUILD_PROBE "Build the headless hitux-probe CLI" ON)

# ---------------------------------------------------------------------------
# MUSCLE (vendored as a submodule, pinned to v9.92, built unmodified).
#
# We only want the library itself; muscled, the tools, the examples and the
# tests are all things we neither ship nor link.  MUSCLE's own Qt support
# library is skipped too -- the only piece of it we use is
# QPostEventCallbackMechanism.h, which is header-only.
# ---------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/muscle/CMakeLists.txt")
    message(FATAL_ERROR
        "third_party/muscle is empty. Run: git submodule update --init --recursive")
endif()

set(WITH_MUSCLED  OFF CACHE BOOL "" FORCE)
set(WITH_TOOLS    OFF CACHE BOOL "" FORCE)
set(WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WITH_TESTS    OFF CACHE BOOL "" FORCE)
set(WITH_QT       OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/muscle EXCLUDE_FROM_ALL)

# MUSCLE's headers are included as "util/String.h", "message/Message.h" and so
# on, so the include root is the muscle directory itself.
set(MUSCLE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/muscle")

add_subdirectory(core)

if(HITUX_BUILD_PROBE)
    add_subdirectory(tools/probe)
endif()

if(HITUX_BUILD_GUI)
    add_subdirectory(qt)
endif()

message(STATUS "")
message(STATUS "HiTuxShare ${PROJECT_VERSION}")
message(STATUS "  build type ...... ${CMAKE_BUILD_TYPE}")
message(STATUS "  Qt 6 GUI ........ ${HITUX_BUILD_GUI}")
message(STATUS "  headless probe .. ${HITUX_BUILD_PROBE}")
message(STATUS "")
