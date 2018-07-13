cmake_minimum_required(VERSION 3.10)
project(FimD C CXX)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# This assumes that gRPC and all its dependencies are already installed on this
# system, so they can be located by find_package().

# Find Protobuf installation
# Looks for protobuf-config.cmake file installed by Protobuf's cmake installation.
set(protobuf_MODULE_COMPATIBLE TRUE)
find_package(Protobuf REQUIRED)
message(STATUS "Using protobuf ${protobuf_VERSION}")

set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)

# Find gRPC installation
# Looks for gRPCConfig.cmake file installed by gRPC's cmake installation.
find_package(gRPC REQUIRED)
message(STATUS "Using gRPC ${gRPC_VERSION}")

set(_GRPC_GRPCPP_UNSECURE gRPC::grpc++)
set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)

include_directories(
	# Include generated *.pb.h files
	"${CMAKE_CURRENT_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}/fimnotify"
    "${PROJECT_SOURCE_DIR}/fim-proto"
)
add_subdirectory(fimnotify fim-proto)

set(_FIM_PROTO_SRCS "${PROJECT_SOURCE_DIR}/fim-proto/fim.pb.cc")
set(_FIM_GRPC_SRCS "${PROJECT_SOURCE_DIR}/fim-proto/fim.grpc.pb.cc")

# Targets
add_executable(fimd "fim_server.cc" ${_FIM_PROTO_SRCS} ${_FIM_GRPC_SRCS})
target_link_libraries(fimd fimnotify ${_PROTOBUF_LIBPROTOBUF} ${_GRPC_GRPCPP_UNSECURE})
