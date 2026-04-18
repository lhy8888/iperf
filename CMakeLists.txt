cmake_minimum_required(VERSION 3.21)

project(iperf LANGUAGES C CXX)

set(IPERF_VERSION "3.21+")

include(CTest)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

option(IPERF_BUILD_GUI "Build the Qt6 Widgets GUI" ON)

include(CheckFunctionExists)
include(CheckIncludeFile)
include(CheckSymbolExists)

include("${CMAKE_SOURCE_DIR}/cmake/windows_compat.cmake")
iperf_prepare_config_headers()

set(IPERF_CORE_SOURCES
    src/cjson.c
    src/dscp.c
    src/iperf_api.c
    src/iperf_auth.c
    src/iperf_client_api.c
    src/iperf_error.c
    src/iperf_locale.c
    src/iperf_pthread.c
    src/iperf_server_api.c
    src/iperf_tcp.c
    src/iperf_time.c
    src/iperf_udp.c
    src/iperf_util.c
    src/net.c
    src/tcp_info.c
    src/timer.c
    src/units.c
    src/platform/win/buffer_alloc.c
    src/platform/win/socket_compat.c
    src/platform/win/win_runtime.c
)

add_library(libiperf STATIC ${IPERF_CORE_SOURCES})
target_include_directories(libiperf
    PUBLIC
        "${CMAKE_BINARY_DIR}/src"
        "${CMAKE_SOURCE_DIR}/src"
)
target_compile_definitions(libiperf
    PRIVATE
        $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
        $<$<PLATFORM_ID:Windows>:NOMINMAX>
        $<$<PLATFORM_ID:Windows>:_WIN32_WINNT=0x0A00>
)

find_package(Threads REQUIRED)
target_link_libraries(libiperf PRIVATE Threads::Threads)

if(WIN32)
    target_link_libraries(libiperf PRIVATE ws2_32 bcrypt)
endif()

if(IPERF_BUILD_GUI)
    find_package(Qt6 COMPONENTS Widgets QUIET)
    if(Qt6_FOUND)
        add_subdirectory(app/iperfwin)
    else()
        message(STATUS "Qt6 Widgets not found, skipping the IperfWin GUI target.")
    endif()
endif()
