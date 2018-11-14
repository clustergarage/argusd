/**
 * MIT License
 * 
 * Copyright (c) 2018 ClusterGarage
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

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
    grpc::Status RecordMetrics(grpc::ServerContext *context, const fim::Empty *request, grpc::ServerWriter<fim::FimdMetricsHandle> *writer) override;

private:
    std::vector<int> getPidsFromRequest(std::shared_ptr<fim::FimdConfig> request);
    std::shared_ptr<fim::FimdHandle> findFimdWatcherByPids(const std::string nodeName, const std::vector<int> pids);
    char **getPathArrayFromSubject(const int pid, std::shared_ptr<fim::FimWatcherSubject> subject);
    char **getPathArrayFromIgnore(std::shared_ptr<fim::FimWatcherSubject> subject);
    static std::string getTagListFromSubject(std::shared_ptr<fim::FimWatcherSubject> subject);
    uint32_t getEventMaskFromSubject(std::shared_ptr<fim::FimWatcherSubject> subject);
    void createInotifyWatcher(std::shared_ptr<fim::FimWatcherSubject> subject, const int pid, const int sid,
        google::protobuf::RepeatedField<google::protobuf::int32> *procFds, const mqd_t mq);
    mqd_t createMessageQueue(const std::string logFormat, const std::string name, const std::string nodeName,
        const std::string podName, const google::protobuf::RepeatedPtrField<fim::FimWatcherSubject> subjects, mqd_t mq);
    static void startMessageQueue(const std::string logFormat, const std::string name, const std::string nodeName,
        const std::string podName, const google::protobuf::RepeatedPtrField<fim::FimWatcherSubject> subjects,
        const mqd_t mq, const std::string mqPath);
    void sendKillSignalToWatcher(std::shared_ptr<fim::FimdHandle> watcher);
    void eraseEventProcessfd(google::protobuf::RepeatedField<google::protobuf::int32> *eventProcessfds, const int processfd);
    void sendExitMessageToMessageQueue(std::shared_ptr<fim::FimdHandle> watcher);

    /**
     * Helper function to remove prepended container protocol from `containerId`
     * given a prefix; currently docker|cri-o|rkt|containerd.
     */
    inline const std::string cleanContainerId(const std::string &containerId, const std::string prefix) const {
        return FimdUtil::eraseSubstr(containerId, prefix + "://");
    }

    static std::string DEFAULT_FORMAT;
    std::vector<std::shared_ptr<fim::FimdHandle>> watchers_;
    static grpc::ServerWriter<fim::FimdMetricsHandle> *metricsWriter_;
};
} // namespace fimd

#endif
