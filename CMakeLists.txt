cmake_minimum_required (VERSION 3.10)
project (FIMnotify)

#include_directories(${PROJECT_BINARY_DIR})

include_directories (
    "${PROJECT_SOURCE_DIR}/fimnotify"
    "${PROJECT_SOURCE_DIR}/fim-proto"
)
add_subdirectory (fimnotify)

add_executable(fimd fim_server.cc)
target_link_libraries(fimd fimnotify libgrpc++)
