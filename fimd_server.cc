#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <grpc/grpc.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>

#include "fimd_impl.h"
#include "fimd_util.h"

using namespace std;
using grpc::Server;
using grpc::ServerBuilder;

#define PORT 50051

int main(int argc, char **argv) {
    stringstream ss;
    ss << "0.0.0.0:" << PORT;
    string server_address(ss.str());
    FimdImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    unique_ptr<grpc::Server> server(builder.BuildAndStart());
    cout << "Server listening on " << server_address << endl;
    server->Wait();
    return 0;
}
