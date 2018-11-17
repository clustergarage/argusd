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
    ~FimdImpl() final = default;

    grpc::Status CreateWatch(grpc::ServerContext *context, const fim::FimdConfig *request, fim::FimdHandle *response) override;
    grpc::Status DestroyWatch(grpc::ServerContext *context, const fim::FimdConfig *request, fim::Empty *response) override;
    grpc::Status GetWatchState(grpc::ServerContext *context, const fim::Empty *request, grpc::ServerWriter<fim::FimdHandle> *writer) override;
    grpc::Status RecordMetrics(grpc::ServerContext *context, const fim::Empty *request, grpc::ServerWriter<fim::FimdMetricsHandle> *writer) override;

private:
    std::vector<int> getPidsFromRequest(std::shared_ptr<fim::FimdConfig> request);
    std::shared_ptr<fim::FimdHandle> findFimdWatcherByPids(std::string nodeName, std::vector<int> pids);
    char **getPathArrayFromSubject(int pid, std::shared_ptr<fim::FimWatcherSubject> subject);
    char **getPathArrayFromIgnore(std::shared_ptr<fim::FimWatcherSubject> subject);
    static std::string getTagListFromSubject(std::shared_ptr<fim::FimWatcherSubject> subject);
    uint32_t getEventMaskFromSubject(std::shared_ptr<fim::FimWatcherSubject> subject);
    void createInotifyWatcher(std::string nodeName, std::string podName, std::shared_ptr<fim::FimWatcherSubject> subject,
        int pid, int sid, google::protobuf::RepeatedField<google::protobuf::int32> *procFds, mqd_t mq);
    mqd_t createMessageQueue(std::string logFormat, std::string name, std::string nodeName,
        std::string podName, google::protobuf::RepeatedPtrField<fim::FimWatcherSubject> subjects, mqd_t mq);
    static void startMessageQueue(std::string logFormat, std::string name, std::string nodeName,
        std::string podName, google::protobuf::RepeatedPtrField<fim::FimWatcherSubject> subjects,
        mqd_t mq, std::string mqPath);
    void sendKillSignalToWatcher(std::shared_ptr<fim::FimdHandle> watcher);
    void eraseEventProcessfd(google::protobuf::RepeatedField<google::protobuf::int32> *eventProcessfds, int processfd);
    void sendExitMessageToMessageQueue(std::shared_ptr<fim::FimdHandle> watcher);

    /**
     * Helper function to remove prepended container protocol from `containerId`
     * given a prefix; currently docker|cri-o|rkt|containerd.
     *
     * @param containerId
     * @param prefix
     */
    inline void cleanContainerId(std::string &containerId, const std::string &prefix) const {
        FimdUtil::eraseSubstr(containerId, prefix + "://");
    }

    std::vector<std::shared_ptr<fim::FimdHandle>> watchers_;
    static grpc::ServerWriter<fim::FimdMetricsHandle> *metricsWriter_;
};
} // namespace fimd

#endif
