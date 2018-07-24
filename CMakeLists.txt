cmake_minimum_required(VERSION 3.10)
project(FimD C CXX)

include(ExternalProject)
set(INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/libs")

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -std=c++17)
set(CMAKE_EXE_LINKER_FLAGS -static)

find_package(Threads)

# Builds protobuf project from the git repo
ExternalProject_Add(zlib
  PREFIX zlib
  GIT_REPOSITORY https://github.com/madler/zlib
  CMAKE_CACHE_ARGS
    -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/zlib
)

# Builds protobuf project from the git submodule
ExternalProject_Add(protobuf
  PREFIX protobuf
  DEPENDS zlib
  GIT_REPOSITORY https://github.com/google/protobuf
  GIT_TAG v3.5.1
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/protobuf/src/protobuf
  BUILD_IN_SOURCE 1
  # SOURCE_SUBDIR cmake/ # Requires CMake 3.7, this will allow removal of CONFIGURE_COMMAND
  # CONFIGURE_COMMAND resets some settings made in CMAKE_CACHE_ARGS and the generator used
  CONFIGURE_COMMAND ${CMAKE_COMMAND} cmake/
    #-DCMAKE_BUILD_TYPE:STRING=Release
    -DCMAKE_VERBOSE_MAKEFILE:BOOL=OFF
    -Dprotobuf_BUILD_TESTS:BOOL=OFF
    -DZLIB_ROOT:STRING=${ZLIB_INSTALL}
    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
  CMAKE_CACHE_ARGS
    -Dprotobuf_BUILD_TESTS:BOOL=OFF
    -Dprotobuf_WITH_ZLIB:BOOL=OFF
    -Dprotobuf_MSVC_STATIC_RUNTIME:BOOL=OFF
    -DZLIB_ROOT:STRING=${ZLIB_INSTALL}
    -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/protobuf
)

# the location where protobuf-config.cmake will be installed varies by platform
set(_FINDPACKAGE_PROTOBUF_CONFIG_DIR ${CMAKE_CURRENT_BINARY_DIR}/protobuf/lib/cmake/protobuf)

# Builds protobuf project from the git repo
ExternalProject_Add(c-ares
  PREFIX c-ares
  GIT_REPOSITORY https://github.com/c-ares/c-ares
  GIT_TAG cares-1_14_0
  CMAKE_CACHE_ARGS
    -DCARES_SHARED:BOOL=OFF
    -DCARES_STATIC:BOOL=ON
    -DCARES_STATIC_PIC:BOOL=ON
    -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/c-ares
)

# if OPENSSL_ROOT_DIR is set, propagate that hint path to the external projects with OpenSSL dependency.
set(_CMAKE_ARGS_OPENSSL_ROOT_DIR "")
if (OPENSSL_ROOT_DIR)
  set(_CMAKE_ARGS_OPENSSL_ROOT_DIR -DOPENSSL_ROOT_DIR:PATH=${OPENSSL_ROOT_DIR})
endif()

# Builds protobuf project from the git repo
ExternalProject_Add(gflags
  PREFIX gflags
  GIT_REPOSITORY https://github.com/gflags/gflags
  GIT_TAG v2.2.1
  CMAKE_CACHE_ARGS
    -DBUILD_SHARED_LIBS:BOOL=OFF
    -DBUILD_STATIC_LIBS:BOOL=ON
    -DBUILD_PACKAGING:BOOL=OFF
    -DBUILD_TESTING:BOOL=OFF
    -DBUILD_NC_TESTS:BOOL=OFF
    -DBUILD_CONFIG_TESTS:BOOL=OFF
    -DBUILD_gflags_LIB:BOOL=ON
    -DINSTALL_HEADERS:BOOL=ON
    -DGFLAGS_NAMESPACE:STRING=google
    -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/gflags
)

# Builds gRPC based on locally checked-out sources and set arguments so that all the dependencies
# are correctly located
ExternalProject_Add(grpc
  PREFIX grpc
  DEPENDS c-ares protobuf zlib
  GIT_REPOSITORY https://github.com/grpc/grpc
  GIT_TAG v1.13.1
  CMAKE_CACHE_ARGS
    -DgRPC_INSTALL:BOOL=ON
    -DgRPC_BUILD_TESTS:BOOL=OFF
    -DgRPC_PROTOBUF_PROVIDER:STRING=package
    -DgRPC_PROTOBUF_PACKAGE_TYPE:STRING=CONFIG
    -DProtobuf_DIR:PATH=${_FINDPACKAGE_PROTOBUF_CONFIG_DIR}
    -DgRPC_ZLIB_PROVIDER:STRING=package
    -DZLIB_ROOT:STRING=${CMAKE_CURRENT_BINARY_DIR}/zlib
    -DgRPC_CARES_PROVIDER:STRING=package
    -Dc-ares_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/c-ares/lib/cmake/c-ares
    -DgRPC_SSL_PROVIDER:STRING=package
    ${_CMAKE_ARGS_OPENSSL_ROOT_DIR}
    -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/grpc
)

# Builds glog project from the git repo
ExternalProject_Add(glog
  PREFIX glog
  DEPENDS gflags
  GIT_REPOSITORY https://github.com/google/glog
  GIT_TAG v0.3.5
  CMAKE_ARGS
    -DCMAKE_CXX_FLAGS="-Wl,--eh-frame-hdr"
  CMAKE_CACHE_ARGS
    -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/glog
)

set(_ZLIB_LIBZLIB ${CMAKE_CURRENT_BINARY_DIR}/zlib/lib/libz.a)
set(_CARES_LIBCARES ${CMAKE_CURRENT_BINARY_DIR}/c-ares/lib/libcares.a)
set(_PROTOBUF_LIBPROTOBUF ${CMAKE_CURRENT_BINARY_DIR}/protobuf/lib/libprotobuf.a)
set(_GFLAGS_LIBGFLAGS ${CMAKE_CURRENT_BINARY_DIR}/gflags/lib/libgflags.a)
set(_GRPC_LIBGRPCPP ${CMAKE_CURRENT_BINARY_DIR}/grpc/lib/libgrpc++_unsecure.a
  ${CMAKE_CURRENT_BINARY_DIR}/grpc/lib/libgrpc_unsecure.a
  ${CMAKE_CURRENT_BINARY_DIR}/grpc/lib/libgpr.a
  ${CMAKE_CURRENT_BINARY_DIR}/grpc/lib/libaddress_sorting.a
)
set(_GLOG_LIBGLOG ${CMAKE_CURRENT_BINARY_DIR}/glog/lib/libglog.a)

include_directories(
  # Include generated *.pb.h files
  ${CMAKE_CURRENT_BINARY_DIR}
  ${PROJECT_SOURCE_DIR}/lib
  ${PROJECT_SOURCE_DIR}/fim-proto
)
add_subdirectory(lib fim-proto)

set(_FIM_PROTO_SRCS ${PROJECT_SOURCE_DIR}/fim-proto/c++/fim.pb.cc)
set(_FIM_GRPC_SRCS ${PROJECT_SOURCE_DIR}/fim-proto/c++/fim.grpc.pb.cc)

# Targets
add_executable(fimd
  fimd_server.cc
  fimd_impl.cc
  fimd_util.cc
  ${_FIM_PROTO_SRCS}
  ${_FIM_GRPC_SRCS}
)
add_dependencies(fimd fimnotify
  grpc
  glog
  protobuf
  gflags
)
target_link_libraries(fimd
  fimnotify
  ${_GRPC_LIBGRPCPP}
  ${_GLOG_LIBGLOG}
  ${_PROTOBUF_LIBPROTOBUF}
  ${_GFLAGS_LIBGFLAGS}
  ${_ZLIB_LIBZLIB}
  ${_CARES_LIBCARES}
  -Wl,--whole-archive ${CMAKE_THREAD_LIBS_INIT} -Wl,--no-whole-archive
  rt
)
