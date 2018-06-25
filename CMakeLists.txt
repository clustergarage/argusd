cmake_minimum_required (VERSION 3.10)
project (FIMnotify)

#include_directories(${PROJECT_BINARY_DIR})

include_directories ("${PROJECT_SOURCE_DIR}/fimnotify")
add_subdirectory (fimnotify)

add_executable(fim-notify-cli src/fimnotify_cli.c)
target_link_libraries(fim-notify-cli fimnotify)
add_executable(fim-notify-server src/fimnotify_server.c)
