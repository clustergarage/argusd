cmake_minimum_required(VERSION 3.11)

if(POLICY CMP0048)
  cmake_policy(SET CMP0048 NEW)
endif()

set(PACKAGE_NAME "ArgusD")
set(PACKAGE_VERSION_MAJOR "0")
set(PACKAGE_VERSION_MINOR "5")
set(PACKAGE_VERSION_PATCH "0")
set(PACKAGE_VERSION "${PACKAGE_VERSION_MAJOR}.${PACKAGE_VERSION_MINOR}.${PACKAGE_VERSION_PATCH}")
set(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_TARNAME "${PACKAGE_NAME}-${PACKAGE_VERSION}")
set(PACKAGE_DESCRIPTION "Argus Daemon - File Integrity Monitoring for Kubernetes")
set(PACKAGE_URL "https://clustergarage.io/argus")
set(PACKAGE_BUGREPORT "https://github.com/clustergarage/argusd/issues")
project(${PACKAGE_NAME} VERSION ${PACKAGE_VERSION} LANGUAGES C CXX)

include(ExternalProject)
include(FetchContent)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

FetchContent_Declare(grpc
  GIT_REPOSITORY https://github.com/grpc/grpc
  GIT_TAG v1.17.2)
FetchContent_GetProperties(grpc)
if(NOT grpc_POPULATED)
  FetchContent_Populate(grpc)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
  set(BUILD_PACKAGING OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  # c-ares
  set(CARES_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(CARES_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(CARES_SHARED OFF CACHE BOOL "" FORCE)
  set(CARES_STATIC ON CACHE BOOL "" FORCE)
  set(CARES_STATIC_PIC ON CACHE BOOL "" FORCE)
  # zlib
  set(BUILD_examples OFF CACHE BOOL "" FORCE)
  set(SKIP_INSTALL_LIBRARIES ON CACHE BOOL "" FORCE)
  # protobuf
  set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
  set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
  # gflags
  set(BUILD_gflags_LIB ON CACHE BOOL "" FORCE)
  set(BUILD_gflags_nothreads_LIB OFF CACHE BOOL "" FORCE)
  set(GFLAGS_NAMESPACE "google" CACHE STRING "" FORCE)
  set(INSTALL_HEADERS ON CACHE BOOL "" FORCE)
  set(INSTALL_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(INSTALL_STATIC_LIBS ON CACHE BOOL "" FORCE)
  # openssl
  set(OPENSSL_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
  # grpc
  set(gRPC_BUILD_CSHARP_EXT OFF CACHE BOOL "" FORCE)
  set(gRPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(gRPC_INSTALL ON CACHE BOOL "" FORCE)
  set(gRPC_USE_PROTO_LITE ON CACHE BOOL "" FORCE)
  add_subdirectory(${grpc_SOURCE_DIR} ${grpc_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

FetchContent_Declare(glog
  GIT_REPOSITORY https://github.com/google/glog
  GIT_TAG v0.3.5)
FetchContent_GetProperties(glog)
if(NOT glog_POPULATED)
  FetchContent_Populate(glog)
  set(WITH_GFLAGS OFF CACHE BOOL "" FORCE)
  set(CMAKE_CXX_FLAGS "-Wl,--eh-frame-hdr" CACHE BOOL "" FORCE)
  add_subdirectory(${glog_SOURCE_DIR} ${glog_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

FetchContent_Declare(fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt
  GIT_TAG 5.2.1)
FetchContent_GetProperties(fmt)
if(NOT fmt_POPULATED)
  FetchContent_Populate(fmt)
  set(FMT_DOC OFF CACHE BOOL "" FORCE)
  set(FMT_TEST OFF CACHE BOOL "" FORCE)
  add_subdirectory(${fmt_SOURCE_DIR} ${fmt_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# Builds libcontainer project from the git repo.
set(LIBCONTAINER_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/libcontainer/include)
set(LIBCONTAINER_LIBRARY ${CMAKE_CURRENT_BINARY_DIR}/libcontainer/lib/libcontainer.a)
ExternalProject_Add(libcontainer
  PREFIX libcontainer
  GIT_REPOSITORY https://github.com/clustergarage/libcontainer
  GIT_TAG v0.1.0
  BUILD_ALWAYS 1
  BUILD_BYPRODUCTS ${LIBCONTAINER_LIBRARY}
  CMAKE_ARGS
    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
)
#FetchContent_Declare(libcontainer
#  PREFIX libcontainer
#  GIT_REPOSITORY https://github.com/clustergarage/libcontainer
#  GIT_TAG v0.1.0)
#FetchContent_GetProperties(libcontainer)
#if(NOT libcontainer_POPULATED)
#  FetchContent_Populate(libcontainer)
#  add_subdirectory(${libcontainer_SOURCE_DIR} ${libcontainer_BINARY_DIR} EXCLUDE_FROM_ALL)
#  set(LIBCONTAINER_INCLUDE_DIR $<TARGET_PROPERTY:libcontainer,INCLUDE_DIRECTORIES>)
#  set(LIBCONTAINER_LIBRARY $<TARGET_FILE:libcontainer>)
#endif()

add_subdirectory(lib)
add_subdirectory(argus-proto)

set(ARGUS_PROTO_SRCS ${PROJECT_SOURCE_DIR}/argus-proto/c++/argus.pb.cc
  ${PROJECT_SOURCE_DIR}/argus-proto/c++/health.pb.cc)
set(ARGUS_GRPC_SRCS ${PROJECT_SOURCE_DIR}/argus-proto/c++/argus.grpc.pb.cc
  ${PROJECT_SOURCE_DIR}/argus-proto/c++/health.grpc.pb.cc)

add_executable(argusd
  src/argusd_server.cc
  src/argusd_impl.cc
  src/argusd_auth.cc
  src/health_impl.cc
  ${ARGUS_PROTO_SRCS}
  ${ARGUS_GRPC_SRCS}
)
add_dependencies(argusd argusnotify libcontainer fmt glog grpc)

target_include_directories(argusd
  # Include headers from directories like <[lib|argus-proto]/file.h>.
  PRIVATE ${CMAKE_SOURCE_DIR}
  PRIVATE ${PROJECT_SOURCE_DIR}/lib
  # Include generated *.pb.h files.
  PRIVATE ${PROJECT_SOURCE_DIR}/argus-proto
  PRIVATE ${LIBCONTAINER_INCLUDE_DIR}
)
target_link_libraries(argusd
  argusnotify
  ${LIBCONTAINER_LIBRARY}
  fmt glog gflags
  grpc++ grpc gpr address_sorting
  libprotobuf ssl crypto
)

# Strip all symbols from built binary.
if(CMAKE_BUILD_TYPE STREQUAL Debug)
  add_custom_command(TARGET argusd POST_BUILD
    COMMAND strip -s $<TARGET_FILE:argusd>)
endif()
