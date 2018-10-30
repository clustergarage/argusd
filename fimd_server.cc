#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <grpc/grpc.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/security/server_credentials.h>

#include "fimd_auth.h"
#include "fimd_impl.h"
#include "fimd_util.h"
#include "health_impl.h"

#define PORT 50051

DEFINE_string(ca_file, "", "SSL CA file");
DEFINE_string(key_file, "", "SSL key file");
DEFINE_string(cert_file, "", "SSL certificate file");
DEFINE_bool(insecure, false, "run server in insecure mode");

int main(int argc, char **argv) {
    google::ParseCommandLineFlags(&argc, &argv, true);
    google::InitGoogleLogging(argv[0]);
    google::InstallFailureSignalHandler();
    FLAGS_stderrthreshold = google::INFO;
    FLAGS_colorlogtostderr = true;

    auto readfile = [](const std::string &filename) -> std::string {
        std::ifstream fh(filename);
        std::stringstream buffer;
        buffer << fh.rdbuf();
        fh.close();
        return buffer.str();
    };

    std::shared_ptr<grpc::ServerCredentials> credentials;
	if (FLAGS_insecure) {
        credentials = grpc::InsecureServerCredentials();
	} else {
		if (FLAGS_cert_file == "" ||
			FLAGS_key_file == "") {
			LOG(WARNING) << "Certificate/private key file not supplied in secure mode (see -insecure flag).";
			return 1;
		}
        std::string key(readfile(FLAGS_key_file));
        std::string cert(readfile(FLAGS_cert_file));
		// The client must present a cert every time a call is made, else it
		// will only happen once when the first connection is made.
		// The other options can be found here:
		// http://www.grpc.io/grpc/core/grpc__security__constants_8h.html#a29ffe63a8bb3b4945ecab42d82758f09
        grpc::SslServerCredentialsOptions sslopts(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
        grpc::SslServerCredentialsOptions::PemKeyCertPair keycert = {key, cert};
        sslopts.pem_key_cert_pairs.push_back(keycert);
		if (FLAGS_ca_file != "") {
			sslopts.pem_root_certs = readfile(FLAGS_ca_file);
		}
        credentials = grpc::SslServerCredentials(sslopts);

		//std::shared_ptr<fimd::FimdAuthMetadataProcessor> authproc(new fimd::FimdAuthMetadataProcessor());
		//credentials->SetAuthMetadataProcessor(authproc);
    }

    std::stringstream ss;
    ss << "0.0.0.0:" << PORT;
    std::string serverAddress(ss.str());
    grpc::ServerBuilder builder;
    builder.AddListeningPort(serverAddress, credentials);

    fimd::FimdImpl fimdSvc;
    builder.RegisterService(&fimdSvc);
    fimdhealth::HealthImpl healthSvc;
    builder.RegisterService(&healthSvc);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    LOG(INFO) << "Server listening on " << serverAddress;
    server->Wait();

    google::ShutdownGoogleLogging();
    google::ShutDownCommandLineFlags();
    return 0;
}
