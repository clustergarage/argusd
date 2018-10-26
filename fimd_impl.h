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
    grpc::Status GetWatchState(grpc::ServerContext *context, const fim::Empty *request, grpc::ServerWriter<fim::FimdHandle> *writer) override;

private:
    std::vector<int> getPidsFromRequest(std::shared_ptr<fim::FimdConfig> request);
    std::shared_ptr<fim::FimdHandle> findFimdWatcherByPids(const std::string nodeName, const std::vector<int> pids);
    char **getPathArrayFromSubject(const int pid, std::shared_ptr<fim::FimWatcherSubject> subject);
    char **getPathArrayFromIgnore(std::shared_ptr<fim::FimWatcherSubject> subject);
    static std::string getTagListFromSubject(std::shared_ptr<fim::FimWatcherSubject> subject);
    uint32_t getEventMaskFromSubject(std::shared_ptr<fim::FimWatcherSubject> subject);
    void createInotifyWatcher(std::shared_ptr<fim::FimWatcherSubject> subject, const int pid, const int sid,
        google::protobuf::RepeatedField<google::protobuf::int32> *procFds, const mqd_t mq);
    mqd_t createMessageQueue(const std::string logFormat, const std::string nodeName, const std::string podName,
        const google::protobuf::RepeatedPtrField<fim::FimWatcherSubject> subjects, mqd_t mq);
    static void startMessageQueue(const std::string logFormat, const std::string nodeName, const std::string podName,
        const google::protobuf::RepeatedPtrField<fim::FimWatcherSubject> subjects, const mqd_t mq, const std::string mqPath);
    void sendKillSignalToWatcher(std::shared_ptr<fim::FimdHandle> watcher);
    void eraseEventProcessfd(google::protobuf::RepeatedField<google::protobuf::int32> *eventProcessfds, const int processfd);
    void sendExitMessageToMessageQueue(std::shared_ptr<fim::FimdHandle> watcher);

    /**
     * helper function to remove prepended container protocol from `containerId`
     * given a prefix; currently docker,cri-o,rkt,containerd
     */
    inline const std::string cleanContainerId(const std::string &containerId, const std::string prefix) const {
        return FimdUtil::eraseSubstr(containerId, prefix + "://");
    }

    static std::string DEFAULT_FORMAT;
    std::vector<std::shared_ptr<fim::FimdHandle>> watchers_;
};
} // namespace fimd

#endif
