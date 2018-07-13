# Proto file
get_filename_component(_FIM_PROTO "fim.proto" ABSOLUTE)
get_filename_component(_FIM_PROTO_PATH "${_FIM_PROTO}" PATH)

# Generated sources
set(_FIM_PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/fim.pb.cc")
set(_FIM_PROTO_HDRS "${CMAKE_CURRENT_BINARY_DIR}/fim.pb.h")
set(_FIM_GRPC_SRCS "${CMAKE_CURRENT_BINARY_DIR}/fim.grpc.pb.cc")
set(_FIM_GRPC_HDRS "${CMAKE_CURRENT_BINARY_DIR}/fim.grpc.pb.h")
add_custom_command(
  OUTPUT "${_FIM_PROTO_SRCS}" "${_FIM_PROTO_HDRS}" "${_FIM_GRPC_SRCS}" "${_FIM_GRPC_HDRS}"
  COMMAND ${_PROTOBUF_PROTOC}
  ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
    --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
    -I "${_FIM_PROTO_PATH}"
    --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
    "${_FIM_PROTO}"
  DEPENDS "${_FIM_PROTO}"
)
