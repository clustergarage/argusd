#ifndef _FIMDIMPL_H
#define _FIMDIMPL_H

#include <future>
#include <thread>
#include <vector>

#include "fim-proto/c++/fim.grpc.pb.h"

class FimdImpl final : public fim::Fimd::Service {
public:
    explicit FimdImpl() = default;
    ~FimdImpl() = default;

    grpc::Status CreateWatch(grpc::ServerContext *context, const fim::FimdConfig *request, fim::FimdHandle *response) override;
    grpc::Status DestroyWatch(grpc::ServerContext *context, const fim::FimdConfig *request, fim::FimdHandle *response) override;

public:
    struct FimdWatcher {
        const int pid;
        const std::vector<std::shared_ptr<std::thread>> threads;
        const std::vector<int> processEventfds;
    };

private:
    std::vector<std::shared_ptr<FimdWatcher>> m_watchers;
};

#endif
