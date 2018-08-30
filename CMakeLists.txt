cmake_minimum_required(VERSION 3.10)
project(FimD C CXX)

include(ExternalProject)

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -std=c++17)
set(CMAKE_EXE_LINKER_FLAGS -static)
set(CMAKE_BUILD_TYPE Release)

find_package(Threads REQUIRED)

# Builds c-ares project from the git repo
ExternalProject_Add(c-ares
  PREFIX c-ares
  GIT_REPOSITORY https://github.com/c-ares/c-ares
  GIT_TAG cares-1_14_0
  CMAKE_CACHE_ARGS
    -DCARES_SHARED:BOOL=OFF
    -DCARES_STATIC:BOOL=ON
    -DCARES_STATIC_PIC:BOOL=ON
    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
)

# Builds zlib project from the git repo
ExternalProject_Add(zlib
  PREFIX zlib
  GIT_REPOSITORY https://github.com/madler/zlib
  GIT_TAG v1.2.11
  UPDATE_COMMAND ""
  CMAKE_CACHE_ARGS
    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
)

# Builds openssl project from the git repo
ExternalProject_Add(openssl
  PREFIX openssl
  GIT_REPOSITORY https://github.com/openssl/openssl
  GIT_TAG OpenSSL_1_1_0
  BUILD_IN_SOURCE 1
  CONFIGURE_COMMAND ./config
    --prefix=${CMAKE_CURRENT_BINARY_DIR}/openssl
    --openssldir=${CMAKE_CURRENT_BINARY_DIR}/openssl
    -fPIC
    no-async no-shared no-unit-test
  INSTALL_COMMAND make install_sw
  CMAKE_CACHE_ARGS
    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
)
ExternalProject_Get_Property(openssl BINARY_DIR)
set(OPENSSL_FOUND TRUE)
set(OPENSSL_ROOT_DIR ${CMAKE_CURRENT_BINARY_DIR}/openssl)
set(OPENSSL_INCLUDE_DIR ${OPENSSL_ROOT_DIR}/include)
set(OPENSSL_CRYPTO_LIBRARY ${OPENSSL_ROOT_DIR}/lib/libcrypto.a)

## Builds protobuf project from the git repo
ExternalProject_Add(protobuf
  PREFIX protobuf
  DEPENDS zlib
  GIT_REPOSITORY https://github.com/google/protobuf
  GIT_TAG v3.5.1
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/protobuf/src/protobuf
  BUILD_IN_SOURCE 1
  CONFIGURE_COMMAND ${CMAKE_COMMAND} cmake/
    -DCMAKE_VERBOSE_MAKEFILE:BOOL=OFF
    -Dprotobuf_BUILD_TESTS:BOOL=OFF
    -DZLIB_ROOT:STRING=${ZLIB_INSTALL}
    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
  CMAKE_CACHE_ARGS
    -Dprotobuf_BUILD_TESTS:BOOL=OFF
    -Dprotobuf_WITH_ZLIB:BOOL=OFF
    -Dprotobuf_MSVC_STATIC_RUNTIME:BOOL=OFF
    -DZLIB_ROOT:STRING=${ZLIB_INSTALL}
    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
)
ExternalProject_Get_Property(protobuf BINARY_DIR)
set(PROTOBUF_FOUND TRUE)
set(PROTOBUF_DIR ${CMAKE_CURRENT_BINARY_DIR}/protobuf)
set(PROTOBUF_INCLUDE_DIR ${PROTOBUF_DIR}/include)
set(PROTOBUF_LIBRARY ${PROTOBUF_DIR}/lib/libprotobuf.a)

# Builds gflags project from the git repo
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
    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
)

# Builds grpc project from the git repo
ExternalProject_Add(grpc
  PREFIX grpc
  DEPENDS c-ares openssl protobuf zlib
  GIT_REPOSITORY https://github.com/grpc/grpc
  GIT_TAG v1.13.1
  CMAKE_CACHE_ARGS
    -DgRPC_INSTALL:BOOL=ON
    -DgRPC_BUILD_TESTS:BOOL=OFF
    -DgRPC_PROTOBUF_PROVIDER:STRING=package
    -DgRPC_PROTOBUF_PACKAGE_TYPE:STRING=CONFIG
    -DProtobuf_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/protobuf/lib/cmake/protobuf
    -DgRPC_ZLIB_PROVIDER:STRING=package
    -DZLIB_ROOT:STRING=${CMAKE_CURRENT_BINARY_DIR}/zlib
    -DgRPC_CARES_PROVIDER:STRING=package
    -Dc-ares_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/c-ares/lib/cmake/c-ares
    -DgRPC_SSL_PROVIDER:STRING=package
    -DOPENSSL_USE_STATIC_LIBS:BOOL=ON
    -DOPENSSL_ROOT_DIR:PATH=${OPENSSL_ROOT_DIR}
    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
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
    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
)

# Builds glog project from the git repo
ExternalProject_Add(fmt
  PREFIX fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt
  GIT_TAG 5.1.0
  CMAKE_CACHE_ARGS
    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
)

set(_CARES_LIBCARES ${CMAKE_CURRENT_BINARY_DIR}/c-ares/lib/libcares.a)
set(_ZLIB_LIBZLIB ${CMAKE_CURRENT_BINARY_DIR}/zlib/lib/libz.a)
set(_PROTOBUF_LIBPROTOBUF ${CMAKE_CURRENT_BINARY_DIR}/protobuf/lib/libprotobuf.a)
set(_GFLAGS_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/gflags/include)
set(_GFLAGS_LIBGFLAGS ${CMAKE_CURRENT_BINARY_DIR}/gflags/lib/libgflags.a)
set(_GLOG_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/glog/include)
set(_GLOG_LIBGLOG ${CMAKE_CURRENT_BINARY_DIR}/glog/lib/libglog.a)
set(_gRPC_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/grpc/include)
set(_gRPC_LIBGRPCPP ${CMAKE_CURRENT_BINARY_DIR}/grpc/lib/libgrpc++_unsecure.a
  ${CMAKE_CURRENT_BINARY_DIR}/grpc/lib/libgrpc_unsecure.a
  ${CMAKE_CURRENT_BINARY_DIR}/grpc/lib/libgpr.a
  ${CMAKE_CURRENT_BINARY_DIR}/grpc/lib/libaddress_sorting.a)
set(_FMT_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/fmt/include)
set(_FMT_LIBFMT ${CMAKE_CURRENT_BINARY_DIR}/fmt/lib/libfmt.a)

include_directories(
  # Include generated *.pb.h files
  ${CMAKE_CURRENT_BINARY_DIR}
  ${PROJECT_SOURCE_DIR}/lib
  ${PROJECT_SOURCE_DIR}/fim-proto
  ${PROTOBUF_INCLUDE_DIR}
  ${_GFLAGS_INCLUDE_DIR}
  ${_GLOG_INCLUDE_DIR}
  ${_gRPC_INCLUDE_DIR}
  ${_FMT_INCLUDE_DIR}
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
add_dependencies(fimd fimnotify grpc glog)
target_link_libraries(fimd
  fimnotify
  ${_FMT_LIBFMT}
  ${_gRPC_LIBGRPCPP}
  ${_GLOG_LIBGLOG}
  ${_GFLAGS_LIBGFLAGS}
  ${_PROTOBUF_LIBPROTOBUF}
  ${_ZLIB_LIBZLIB}
  ${_CARES_LIBCARES}
  -Wl,--whole-archive ${CMAKE_THREAD_LIBS_INIT} -Wl,--no-whole-archive
  rt
)

# strip unnecessary symbols
add_custom_command(TARGET fimd POST_BUILD
  COMMAND strip -s $<TARGET_FILE:fimd>)
