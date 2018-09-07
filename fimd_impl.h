#ifndef _FIMDIMPL_H
#define _FIMDIMPL_H

#include <mqueue.h>
#include <future>
#include <vector>

#include "fimd_util.h"
#include "fim-proto/c++/fim.grpc.pb.h"

namespace fimd {
class FimdImpl final : public fim::Fimd::Service {
public:
    explicit FimdImpl() = default;
    ~FimdImpl() = default;

    grpc::Status CreateWatch(grpc::ServerContext *context, const fim::FimdConfig *request, fim::FimdHandle *response) override;
    grpc::Status DestroyWatch(grpc::ServerContext *context, const fim::FimdConfig *request, fim::Empty *response) override;

private:
    std::vector<int> getPidsFromRequest(const fim::FimdConfig *request);
    std::shared_ptr<fim::FimdHandle> findFimdWatcherByPids(const std::string nodeName, const std::vector<int> pids);
    char **getPathArrayFromSubject(const int pid, const fim::FimWatcherSubject subject);
    uint32_t getEventMaskFromSubject(const fim::FimWatcherSubject subject);
    void createInotifyWatcher(const fim::FimWatcherSubject subject, const int pid,
        google::protobuf::RepeatedField<google::protobuf::int32> *procFds);
    mqd_t createMessageQueue(const std::string logFormat, const std::string nodeName, const std::string podName, bool recreate);
    static void startMessageQueue(const std::string logFormat, const std::string nodeName, const std::string podName, mqd_t mq);
    void sendKillSignalToWatcher(std::shared_ptr<fim::FimdHandle> watcher);
    void eraseEventProcessfd(google::protobuf::RepeatedField<google::protobuf::int32> *eventProcessfds, const int processfd);
    void sendExitMessageToMessageQueue(std::shared_ptr<fim::FimdHandle> watcher);

    inline const std::string cleanContainerId(const std::string &containerId) const {
        return FimdUtil::eraseSubstr(containerId, "docker://");
    }

    static std::string DEFAULT_FORMAT;

    std::vector<std::shared_ptr<fim::FimdHandle>> watchers_;
    mqd_t mq_;
};
} // namespace fimd

#endif
