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

add_executable(fimctl client.c)
target_link_libraries(fimctl fimnotify commander amp)

add_executable(fimd server.c)
target_link_libraries(fimd fimnotify commander amp)
