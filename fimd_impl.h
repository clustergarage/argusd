#ifndef _FIMDIMPL_H
#define _FIMDIMPL_H

#include <future>
#include <thread>
#include <vector>

#include "fim-proto/fim.grpc.pb.h"

class FimdImpl final : public fim::Fimd::Service {
public:
    explicit FimdImpl() = default;
    ~FimdImpl() = default;

    grpc::Status NewWatch(grpc::ServerContext *context, const fim::FimdConfig *request, fim::FimdHandle *response) override;

private:
    std::vector<std::thread *> m_watchers;
};

#endif
