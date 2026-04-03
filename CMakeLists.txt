cmake_minimum_required(VERSION 3.20)
project(remotepad LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# nlohmann_json from local deps (header-only)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/deps/json EXCLUDE_FROM_ALL)

# libuv (required on Windows as the uSockets event-loop backend)
include(FetchContent)
FetchContent_Declare(
    libuv
    URL https://github.com/libuv/libuv/archive/refs/tags/v1.49.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(LIBUV_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libuv)

# Build uSockets as a static library from local deps
file(GLOB USOCKETS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/deps/uSockets/src/*.c)
if(WIN32)
    list(APPEND USOCKETS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/deps/uSockets/src/eventing/libuv.c)
endif()
add_library(uSockets STATIC ${USOCKETS_SOURCES})
target_include_directories(uSockets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/deps/uSockets/src)
target_compile_definitions(uSockets PUBLIC LIBUS_NO_SSL)
if(WIN32)
    target_compile_definitions(uSockets PUBLIC LIBUS_USE_LIBUV)
    target_include_directories(uSockets PUBLIC ${libuv_SOURCE_DIR}/include)
    target_link_libraries(uSockets PUBLIC ws2_32 uv_a iphlpapi userenv)
endif()

# uWebSockets is header-only (no ZLIB — compression disabled via UWS_NO_ZLIB)
add_library(uWebSockets INTERFACE)
target_include_directories(uWebSockets INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/deps/uWebSockets/src)
target_compile_definitions(uWebSockets INTERFACE UWS_NO_ZLIB)
target_link_libraries(uWebSockets INTERFACE uSockets)

# Main executable
set(COMMON_SOURCES
    src/app_config.cpp
    src/draw_color.cpp
    src/main.cpp
    src/remote_pad.cpp
    src/web_server.cpp
)

set(PLATFORM_SOURCES)
set(PLATFORM_LIBS)

if(WIN32)
    list(APPEND PLATFORM_SOURCES
        src/platform/windows/win_overlay.cpp
        src/platform/windows/win_screen_capture.cpp
    )
    list(APPEND PLATFORM_LIBS
        gdiplus
        user32
        gdi32
    )
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
    pkg_check_modules(XFIXES REQUIRED IMPORTED_TARGET xfixes)
    pkg_check_modules(XEXT REQUIRED IMPORTED_TARGET xext)
    find_package(JPEG REQUIRED)

    list(APPEND PLATFORM_SOURCES
        src/platform/linux/linux_overlay.cpp
        src/platform/linux/linux_screen_capture.cpp
        src/x_screen.cpp
        src/x_tools.cpp
    )
    list(APPEND PLATFORM_LIBS
        PkgConfig::X11
        PkgConfig::XFIXES
        PkgConfig::XEXT
        JPEG::JPEG
    )
endif()

add_executable(remotepad
    ${COMMON_SOURCES}
    ${PLATFORM_SOURCES}
)

target_include_directories(remotepad PRIVATE src)

target_link_libraries(remotepad PRIVATE
    ${PLATFORM_LIBS}
    uWebSockets
    nlohmann_json::nlohmann_json
)

target_compile_definitions(remotepad PRIVATE LIBUS_NO_SSL)
if(WIN32)
    target_compile_definitions(remotepad PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# Embed client files into the binary
option(EMBED_CLIENT "Embed client files in binary" ON)
if(EMBED_CLIENT)
    set(EMBED_HEADER "${CMAKE_BINARY_DIR}/embedded_client_data.h")
    if(WIN32)
        add_custom_command(
            OUTPUT "${EMBED_HEADER}"
            COMMAND powershell -ExecutionPolicy Bypass -File
                    "${CMAKE_SOURCE_DIR}/tools/embed_client.ps1"
                    -ClientDir "${CMAKE_SOURCE_DIR}/client"
                    -Output "${EMBED_HEADER}"
            DEPENDS ${CMAKE_SOURCE_DIR}/client/index.html
                    ${CMAKE_SOURCE_DIR}/client/client.js
                    ${CMAKE_SOURCE_DIR}/client/draw.css
                    ${CMAKE_SOURCE_DIR}/tools/embed_client.ps1
            COMMENT "Embedding client files"
        )
    else()
        add_custom_command(
            OUTPUT "${EMBED_HEADER}"
            COMMAND bash "${CMAKE_SOURCE_DIR}/tools/embed_client.sh"
                    "${CMAKE_SOURCE_DIR}/client" "${EMBED_HEADER}"
            DEPENDS ${CMAKE_SOURCE_DIR}/client/index.html
                    ${CMAKE_SOURCE_DIR}/client/client.js
                    ${CMAKE_SOURCE_DIR}/client/draw.css
                    ${CMAKE_SOURCE_DIR}/tools/embed_client.sh
            COMMENT "Embedding client files"
        )
    endif()
    add_custom_target(embed_client DEPENDS "${EMBED_HEADER}")
    add_dependencies(remotepad embed_client)
    target_include_directories(remotepad PRIVATE "${CMAKE_BINARY_DIR}")
    target_compile_definitions(remotepad PRIVATE EMBED_CLIENT)
endif()
