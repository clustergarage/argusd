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

DEFINE_bool(tls, false, "Run server in secure mode");
DEFINE_string(tlscafile, "", "Root CA used for mutual TLS");
DEFINE_string(tlscertfile, "", "Certificate for mutual TLS");
DEFINE_string(tlskeyfile, "", "Private key for mutual TLS");

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
	if (FLAGS_tls) {
		if (FLAGS_tlscertfile == "" ||
			FLAGS_tlskeyfile == "") {
			LOG(WARNING) << "Certificate/private key not supplied (with -tls flag).";
			return 1;
		}
        std::string key(readfile(FLAGS_tlskeyfile));
        std::string cert(readfile(FLAGS_tlscertfile));
		// The client must present a cert every time a call is made, else it
		// will only happen once when the first connection is made.
		// The other options can be found here:
		// http://www.grpc.io/grpc/core/grpc__security__constants_8h.html#a29ffe63a8bb3b4945ecab42d82758f09
        grpc::SslServerCredentialsOptions sslopts(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
        grpc::SslServerCredentialsOptions::PemKeyCertPair keycert = {key, cert};
        sslopts.pem_key_cert_pairs.push_back(keycert);
		if (FLAGS_tlscafile != "") {
			sslopts.pem_root_certs = readfile(FLAGS_tlscafile);
		}
        credentials = grpc::SslServerCredentials(sslopts);

		//std::shared_ptr<fimd::FimdAuthMetadataProcessor> authproc(new fimd::FimdAuthMetadataProcessor());
		//credentials->SetAuthMetadataProcessor(authproc);
	} else {
        credentials = grpc::InsecureServerCredentials();
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
