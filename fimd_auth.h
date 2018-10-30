#ifndef _FIMD_AUTH_H
#define _FIMD_AUTH_H

#include <grpc/grpc.h>
#include <grpc++/grpc++.h>

namespace fimd {
class FimdAuthMetadataProcessor : public grpc::AuthMetadataProcessor {
public:
    grpc::Status Process(const grpc::AuthMetadataProcessor::InputMetadata &authMetadata, grpc::AuthContext *context,
        grpc::AuthMetadataProcessor::OutputMetadata *consumedAuthMetadata,
        grpc::AuthMetadataProcessor::OutputMetadata *responseMetadata) GRPC_OVERRIDE;
};
} // namespace fimd
#endif
