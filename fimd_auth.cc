#include "fimd_auth.h"

#include <string>

#include <grpc/grpc.h>
#include <grpc++/grpc++.h>

namespace fimd {
grpc::Status FimdAuthMetadataProcessor::Process(const grpc::AuthMetadataProcessor::InputMetadata &authMetadata, grpc::AuthContext *context,
    grpc::AuthMetadataProcessor::OutputMetadata *consumedAuthMetadata, grpc::AuthMetadataProcessor::OutputMetadata *responseMetadata) {

    for (auto it = authMetadata.begin(); it != authMetadata.end(); ++it) {
        std::string key(it->first.begin(), it->first.end());
        std::string val(it->second.begin(), it->second.end());
    }
    return grpc::Status::OK;
}
} // namespace fimd
