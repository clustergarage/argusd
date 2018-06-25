cmake_minimum_required (VERSION 3.10)
project (FIMnotify)

#include_directories(${PROJECT_BINARY_DIR})

include_directories (
    "${PROJECT_SOURCE_DIR}/fimnotify"
    "${PROJECT_SOURCE_DIR}/deps/amp"
    "${PROJECT_SOURCE_DIR}/deps/commander"
    )
add_subdirectory (fimnotify)
add_subdirectory (deps)

add_executable(fim-notify-client src/fimnotify_client.c)
target_link_libraries(fim-notify-client fimnotify commander amp)

add_executable(fim-notify-cli src/fimnotify_cli.c)
target_link_libraries(fim-notify-cli fimnotify commander amp)

add_executable(fim-notify-server src/fimnotify_server.c)
target_link_libraries(fim-notify-server fimnotify commander amp)
