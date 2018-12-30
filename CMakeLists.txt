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
set(PACKAGE_BUGREPORT "https://github.com/clustergarage/argusd/issues")
set(PACKAGE_DESCRIPTION "Argus Daemon - File Integrity Monitoring for Kubernetes")
set(PACKAGE_URL "https://clustergarage.io/argus")
project(${PACKAGE_NAME} VERSION ${PACKAGE_VERSION} LANGUAGES C CXX)
#project(ArgusD VERSION 0.5.0 LANGUAGES C CXX)

include(ExternalProject)
include(FetchContent)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXE_LINKER_FLAGS "-static")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(BUILD_SHARED_LIBS OFF)
set(BUILD_STATIC_LIBS ON)
set(BUILD_PACKAGING OFF)
set(BUILD_TESTING OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)
find_library(DL_LIBRARY NAMES libdl.a dl REQUIRED)
find_library(RT_LIBRARY NAMES librt.a rt REQUIRED)
find_library(PTHREAD_LIBRARY NAMES libpthread.a pthread REQUIRED)

# Builds c-ares project from the git repo.
#set(CARES_LIBRARY ${CMAKE_CURRENT_BINARY_DIR}/c-ares/lib/libcares.a)
#ExternalProject_Add(c-ares
#  PREFIX c-ares
#  GIT_REPOSITORY https://github.com/c-ares/c-ares
#  GIT_TAG cares-1_15_0
#  #UPDATE_COMMAND ""
#  #TEST_COMMAND ""
#  #TEST_BEFORE_INSTALL 0
#  #TEST_AFTER_INSTALL 0
#  BUILD_ALWAYS 0
#  BUILD_BYPRODUCTS ${CARES_LIBRARY}
#  CMAKE_ARGS
#    -DCARES_STATIC:BOOL=ON
#    -DCARES_STATIC_PIC:BOOL=ON
#    -DCARES_SHARED:BOOL=OFF
#    -DCARES_BUILD_TESTS:BOOL=OFF
#    -DCARES_BUILD_TOOLS:BOOL=OFF
#    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
#)

# Builds zlib project from the git repo.
#set(ZLIB_LIBRARY ${CMAKE_CURRENT_BINARY_DIR}/zlib/lib/libz.a)
#ExternalProject_Add(zlib
#  PREFIX zlib
#  GIT_REPOSITORY https://github.com/madler/zlib
#  GIT_TAG v1.2.11
#  #UPDATE_COMMAND ""
#  #TEST_COMMAND ""
#  #TEST_BEFORE_INSTALL 0
#  #TEST_AFTER_INSTALL 0
#  BUILD_ALWAYS 0
#  BUILD_BYPRODUCTS ${ZLIB_LIBRARY}
#  CMAKE_ARGS
#    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
#)

# Builds openssl project from the git repo.
#set(OPENSSL_ROOT_DIR ${CMAKE_CURRENT_BINARY_DIR}/openssl)
#set(OPENSSL_INCLUDE_DIR ${OPENSSL_ROOT_DIR}/include)
#set(OPENSSL_LIBRARY ${OPENSSL_ROOT_DIR}/lib/libssl.a)
#set(OPENSSL_CRYPTO_LIBRARY ${OPENSSL_ROOT_DIR}/lib/libcrypto.a)
#ExternalProject_Add(openssl
#  PREFIX openssl
#  GIT_REPOSITORY https://github.com/openssl/openssl
#  GIT_TAG OpenSSL_1_1_1
#  #UPDATE_COMMAND ""
#  #TEST_COMMAND ""
#  #TEST_BEFORE_INSTALL 0
#  #TEST_AFTER_INSTALL 0
#  BUILD_ALWAYS 0
#  BUILD_IN_SOURCE 1
#  BUILD_BYPRODUCTS ${OPENSSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY}
#  CONFIGURE_COMMAND ./config
#    --prefix=${CMAKE_CURRENT_BINARY_DIR}/openssl
#    --openssldir=${CMAKE_CURRENT_BINARY_DIR}/openssl
#    -fPIC
#    no-async no-shared no-unit-test
#  INSTALL_COMMAND make install_sw
#  CMAKE_ARGS
#    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
#)
#ExternalProject_Get_Property(openssl BINARY_DIR)
#set(OPENSSL_FOUND TRUE)

## Builds protobuf project from the git repo.
#set(PROTOBUF_ROOT_DIR ${CMAKE_CURRENT_BINARY_DIR}/protobuf)
#set(PROTOBUF_INCLUDE_DIR ${PROTOBUF_ROOT_DIR}/include)
#set(PROTOBUF_LIBRARY ${PROTOBUF_ROOT_DIR}/lib/libprotobuf.a)
#ExternalProject_Add(protobuf
#  PREFIX protobuf
#  #DEPENDS zlib
#  GIT_REPOSITORY https://github.com/google/protobuf
#  GIT_TAG v3.6.1
#  #UPDATE_COMMAND ""
#  #TEST_COMMAND ""
#  #TEST_BEFORE_INSTALL 0
#  #TEST_AFTER_INSTALL 0
#  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/protobuf/src/protobuf
#  BUILD_ALWAYS 0
#  BUILD_IN_SOURCE 1
#  BUILD_BYPRODUCTS ${PROTOBUF_LIBRARY}
#  CONFIGURE_COMMAND ${CMAKE_COMMAND} cmake/
#    -Dprotobuf_BUILD_SHARED_LIBS:BOOL=OFF
#    -Dprotobuf_BUILD_TESTS:BOOL=OFF
#    -Dprotobuf_BUILD_EXAMPLES:BOOL=OFF
#    -Dprotobuf_WITH_ZLIB:BOOL=OFF
#    -Dprotobuf_MSVC_STATIC_RUNTIME:BOOL=OFF
#    -DCMAKE_VERBOSE_MAKEFILE:BOOL=OFF
#    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
#  CMAKE_ARGS
#    -Dprotobuf_BUILD_SHARED_LIBS:BOOL=OFF
#    -Dprotobuf_BUILD_TESTS:BOOL=OFF
#    -Dprotobuf_BUILD_EXAMPLES:BOOL=OFF
#    -Dprotobuf_WITH_ZLIB:BOOL=OFF
#    -Dprotobuf_MSVC_STATIC_RUNTIME:BOOL=OFF
#    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
#)

# Builds gflags project from the git repo.
#set(GFLAGS_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/gflags/include)
#set(GFLAGS_LIBRARY ${CMAKE_CURRENT_BINARY_DIR}/gflags/lib/libgflags.a)
#ExternalProject_Add(gflags
#  PREFIX gflags
#  GIT_REPOSITORY https://github.com/gflags/gflags
#  GIT_TAG v2.2.2
#  #UPDATE_COMMAND ""
#  #TEST_COMMAND ""
#  #TEST_BEFORE_INSTALL 0
#  #TEST_AFTER_INSTALL 0
#  BUILD_ALWAYS 0
#  BUILD_BYPRODUCTS ${GFLAGS_LIBRARY}
#  CMAKE_ARGS
#    -DBUILD_SHARED_LIBS:BOOL=OFF
#    -DBUILD_STATIC_LIBS:BOOL=ON
#    -DBUILD_PACKAGING:BOOL=OFF
#    -DBUILD_TESTING:BOOL=OFF
#    #-DBUILD_NC_TESTS:BOOL=OFF
#    #-DBUILD_CONFIG_TESTS:BOOL=OFF
#    -DBUILD_gflags_LIB:BOOL=ON
#    -DINSTALL_HEADERS:BOOL=ON
#    -DGFLAGS_NAMESPACE:STRING=google
#    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
#)

# Builds grpc project from the git repo.
#set(gRPC_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/grpc/include)
#set(gRPC_LIBRARY ${CMAKE_CURRENT_BINARY_DIR}/grpc/lib/libgrpc++.a
#  ${CMAKE_CURRENT_BINARY_DIR}/grpc/lib/libgrpc.a
#  ${CMAKE_CURRENT_BINARY_DIR}/grpc/lib/libgpr.a
#  ${CMAKE_CURRENT_BINARY_DIR}/grpc/lib/libaddress_sorting.a)
#ExternalProject_Add(grpc
#  PREFIX grpc
#  DEPENDS openssl protobuf #c-ares gflags zlib
#  GIT_REPOSITORY https://github.com/grpc/grpc
#  GIT_TAG v1.16.1
#  #UPDATE_COMMAND ""
#  #TEST_COMMAND ""
#  #TEST_BEFORE_INSTALL 0
#  #TEST_AFTER_INSTALL 0
#  BUILD_ALWAYS 0
#  BUILD_BYPRODUCTS ${gRPC_LIBRARY}
#  CMAKE_ARGS
#    -DgRPC_BUILD_TESTS:BOOL=OFF
#    -DgRPC_BUILD_CSHARP_EXT:BOOL=OFF
#    -DgRPC_USE_PROTO_LITE:BOOL=ON
#    -DgRPC_INSTALL:BOOL=ON
#    #-DgRPC_CARES_PROVIDER:STRING=package
#    #-DgRPC_GFLAGS_PROVIDER:STRING=package
#    -DgRPC_PROTOBUF_PROVIDER:STRING=package
#    -DgRPC_PROTOBUF_PACKAGE_TYPE:STRING=CONFIG
#    -DgRPC_SSL_PROVIDER:STRING=package
#    #-DgRPC_ZLIB_PROVIDER:STRING=package
#    #-Dc-ares_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/c-ares/lib/cmake/c-ares
#    #-Dc-ares_DIR:PATH=${c-ares_BUILD_DIR}
#    -DProtobuf_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/protobuf/lib/cmake/protobuf
#    #-DZLIB_ROOT:PATH=${CMAKE_CURRENT_BINARY_DIR}/zlib
#    #-DZLIB_ROOT:PATH=$<TARGET_PROPERTY:zlib,INCLUDE_DIRECTORIES>
#    -DOPENSSL_USE_STATIC_LIBS:BOOL=ON
#    -DOPENSSL_ROOT_DIR:PATH=${OPENSSL_ROOT_DIR}
#    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
#)
FetchContent_Declare(grpc
  GIT_REPOSITORY https://github.com/grpc/grpc
  GIT_TAG v1.17.2
)
FetchContent_GetProperties(grpc)
if(NOT grpc_POPULATED)
  FetchContent_Populate(grpc)
  # c-ares flags
  set(CARES_STATIC ON CACHE BOOL "" FORCE)
  set(CARES_STATIC_PIC ON CACHE BOOL "" FORCE)
  set(CARES_SHARED OFF CACHE BOOL "" FORCE)
  set(CARES_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(CARES_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  # zlib flags
  set(SKIP_INSTALL_LIBRARIES ON CACHE BOOL "" FORCE)
  set(BUILD_examples OFF CACHE BOOL "" FORCE)
  #set(ZLIB_INCLUDE_DIR $<TARGET_PROPERTY:zlibstatic,INCLUDE_DIRECTORIES>)
  #set(ZLIB_LIBRARY $<TARGET_FILE:zlibstatic>)
  # protobuf flags
  set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
  set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
  # gflags flags
  set(BUILD_gflags_LIB ON CACHE BOOL "" FORCE)
  set(BUILD_gflags_nothreads_LIB OFF CACHE BOOL "" FORCE)
  set(INSTALL_HEADERS ON CACHE BOOL "" FORCE)
  set(INSTALL_STATIC_LIBS ON CACHE BOOL "" FORCE)
  set(INSTALL_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(GFLAGS_NAMESPACE "google" CACHE STRING "" FORCE)
  # openssl flags
  set(OPENSSL_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
  # grpc flags
  set(gRPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(gRPC_BUILD_CSHARP_EXT OFF CACHE BOOL "" FORCE)
  set(gRPC_USE_PROTO_LITE ON CACHE BOOL "" FORCE)
  set(gRPC_INSTALL ON CACHE BOOL "" FORCE)

  #set(c-ares_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/c-ares/lib/cmake/c-ares)
  #set(Protobuf_DIR ${CMAKE_CURRENT_BINARY_DIR}/protobuf/lib/cmake/protobuf CACHE PATH "" FORCE)
  #set(Protobuf_DIR "${protobuf_SOURCE_DIR}/cmake" CACHE PATH "" FORCE)
  #set(ZLIB_ROOT:PATH=${CMAKE_CURRENT_BINARY_DIR}/zlib)
  #set(OPENSSL_ROOT_DIR ${OPENSSL_ROOT_DIR} CACHE PATH "" FORCE)

  add_subdirectory(${grpc_SOURCE_DIR} ${grpc_BINARY_DIR} EXCLUDE_FROM_ALL)
  #add_dependencies(grpc c-ares gflags libprotobuf ssl zlib)
endif()

# Builds glog project from the git repo.
#set(GLOG_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/glog/include)
#set(GLOG_LIBRARY ${CMAKE_CURRENT_BINARY_DIR}/glog/lib/libglog.a)
#ExternalProject_Add(glog
#  PREFIX glog
#  DEPENDS gflags
#  GIT_REPOSITORY https://github.com/google/glog
#  GIT_TAG v0.3.5
#  #UPDATE_COMMAND ""
#  #TEST_COMMAND ""
#  #TEST_BEFORE_INSTALL 0
#  #TEST_AFTER_INSTALL 0
#  BUILD_ALWAYS 0
#  BUILD_BYPRODUCTS ${GLOG_LIBRARY}
#  CMAKE_ARGS
#    -DBUILD_SHARED_LIBS:BOOL=OFF
#    -DBUILD_TESTING:BOOL=OFF
#    -DCMAKE_CXX_FLAGS="-Wl,--eh-frame-hdr"
#    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
#)
FetchContent_Declare(glog
  GIT_REPOSITORY https://github.com/google/glog
  GIT_TAG v0.3.5
)
FetchContent_GetProperties(glog)
if(NOT glog_POPULATED)
  FetchContent_Populate(glog)
  set(WITH_GFLAGS OFF CACHE BOOL "" FORCE)
  set(CMAKE_CXX_FLAGS "-Wl,--eh-frame-hdr" CACHE BOOL "" FORCE)
  add_subdirectory(${glog_SOURCE_DIR} ${glog_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# Builds fmt project from the git repo.
#set(FMT_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/fmt/include)
#set(FMT_LIBRARY ${CMAKE_CURRENT_BINARY_DIR}/fmt/lib/libfmt.a)
#ExternalProject_Add(fmt
#  PREFIX fmt
#  GIT_REPOSITORY https://github.com/fmtlib/fmt
#  GIT_TAG 5.2.1
#  #UPDATE_COMMAND ""
#  #TEST_COMMAND ""
#  #TEST_BEFORE_INSTALL 0
#  #TEST_AFTER_INSTALL 0
#  BUILD_ALWAYS 0
#  BUILD_BYPRODUCTS ${FMT_LIBRARY}
#  CMAKE_ARGS
#    -DFMT_DOC:BOOL=OFF
#    -DFMT_TEST:BOOL=OFF
#    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
#)
FetchContent_Declare(fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt
  GIT_TAG 5.2.1
)
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
#  GIT_REPOSITORY https://github.com/clustergarage/libcontainer
#  GIT_TAG v0.1.0
#)
#FetchContent_GetProperties(libcontainer)
#if(NOT libcontainer_POPULATED)
#  FetchContent_Populate(libcontainer)
#  add_subdirectory(${libcontainer_SOURCE_DIR} ${libcontainer_BINARY_DIR}) # EXCLUDE_FROM_ALL)
#endif()

include_directories(${CMAKE_CURRENT_BINARY_DIR}
  ${PROJECT_SOURCE_DIR}/lib
  # Include generated *.pb.h files.
  ${PROJECT_SOURCE_DIR}/argus-proto
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  #PRIVATE ${GLOG_INCLUDE_DIR}
  #PRIVATE ${gRPC_INCLUDE_DIR}
  #PRIVATE ${FMT_INCLUDE_DIR}
  PRIVATE ${LIBCONTAINER_INCLUDE_DIR}
)
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
add_dependencies(argusd argusnotify glog grpc fmt) # libcontainer

target_include_directories(argusd
  # Include headers from directories like <[lib|argus-proto]/file.h>.
  PRIVATE ${CMAKE_SOURCE_DIR}
  #PRIVATE ${libcontainer_SOURCE_DIR}/include
)
target_link_libraries(argusd
  argusnotify
  ${LIBCONTAINER_LIBRARY} #libcontainer
  fmt #${FMT_LIBRARY}
  grpc++ grpc gpr address_sorting #${gRPC_LIBRARY}
  glog #${GLOG_LIBRARY}
  gflags #${_gRPC_GFLAGS_LIBRARIES} #${GFLAGS_LIBRARY}
  libprotobuf #${_gRPC_PROTOBUF_LIBRARIES} #${PROTOBUF_LIBRARY} #libprotobuf
  ssl crypto #${_gRPC_SSL_LIBRARIES} #${OPENSSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} #ssl crypto
  #${ZLIB_LIBRARY}
  #${CARES_LIBRARY}
  -Wl,--whole-archive ${CMAKE_THREAD_LIBS_INIT} -Wl,--no-whole-archive
  ${RT_LIBRARY}
  ${PTHREAD_LIBRARY}
  ${DL_LIBRARY}
)

# Strip all symbols from built binary.
add_custom_command(TARGET argusd POST_BUILD
  COMMAND strip -s $<TARGET_FILE:argusd>)
