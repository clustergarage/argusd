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

    struct FimdWatcher {
        const int pid;
        std::vector<int> processEventfds;
    };

    grpc::Status CreateWatch(grpc::ServerContext *context, const fim::FimdConfig *request, fim::FimdHandle *response) override;
    grpc::Status DestroyWatch(grpc::ServerContext *context, const fim::FimdConfig *request, fim::FimdHandle *response) override;

private:
    std::shared_ptr<FimdWatcher> findFimdWatcherByPid(const int pid);
	char **getPathArrayFromSubject(const int pid, const fim::FimWatcherSubject subject);
	uint32_t getEventMaskFromSubject(const fim::FimWatcherSubject subject);
	void createInotifyWatcher(const fim::FimWatcherSubject subject, char **patharr, uint32_t event_mask, std::vector<int> *procFds);
	void sendKillSignalToWatcher(std::shared_ptr<FimdWatcher> watcher);

private:
    std::vector<std::shared_ptr<FimdWatcher>> m_watchers;
};

#endif
