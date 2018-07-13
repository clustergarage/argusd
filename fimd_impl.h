#ifndef _FIMDIMPL_H
#define _FIMDIMPL_H

#include "fim-proto/fim.grpc.pb.h"

class FimdImpl final : public fim::Fimd::Service {
public:
    explicit FimdImpl() = default;
    ~FimdImpl() = default;

    grpc::Status NewWatch(grpc::ServerContext *context, const fim::FimdConfig *request, fim::FimdHandle *response) override;

private:
    // @TODO: vector of watchers to use for kill/modify operations
};

#endif
