#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <glog/logging.h>
#include <grpc/grpc.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>

#include "fimd_impl.h"
#include "fimd_util.h"

#define PORT 50051

int main(int argc, char **argv) {
    google::InitGoogleLogging(argv[0]);
#if DEBUG
    google::InstallFailureSignalHandler();
#endif
    FLAGS_stderrthreshold = google::INFO;
    FLAGS_colorlogtostderr = true;

    std::stringstream ss;
    ss << "0.0.0.0:" << PORT;
    std::string server_address(ss.str());
    fimd::FimdImpl service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    LOG(INFO) << "Server listening on " << server_address;
    server->Wait();

    google::ShutdownGoogleLogging();
    return 0;
}
