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

#ifndef __ARGUSD_IMPL_H__
#define __ARGUSD_IMPL_H__

#include <mqueue.h>
#include <future>
#include <vector>

#include "argusd_util.h"
#include <argus-proto/c++/argus.grpc.pb.h>

namespace argusd {
class ArgusdImpl final : public argus::Argusd::Service {
public:
    explicit ArgusdImpl() = default;
    ~ArgusdImpl() final = default;

    grpc::Status CreateWatch(grpc::ServerContext *context, const argus::ArgusdConfig *request, argus::ArgusdHandle *response) override;
    grpc::Status DestroyWatch(grpc::ServerContext *context, const argus::ArgusdConfig *request, argus::Empty *response) override;
    grpc::Status GetWatchState(grpc::ServerContext *context, const argus::Empty *request, grpc::ServerWriter<argus::ArgusdHandle> *writer) override;
    grpc::Status RecordMetrics(grpc::ServerContext *context, const argus::Empty *request, grpc::ServerWriter<argus::ArgusdMetricsHandle> *writer) override;

private:
    std::vector<int> getPidsFromRequest(std::shared_ptr<argus::ArgusdConfig> request);
    std::shared_ptr<argus::ArgusdHandle> findArgusdWatcherByPids(std::string nodeName, std::vector<int> pids);
    char **getPathArrayFromSubject(int pid, std::shared_ptr<argus::ArgusWatcherSubject> subject);
    char **getPathArrayFromIgnore(std::shared_ptr<argus::ArgusWatcherSubject> subject);
    static std::string getTagListFromSubject(std::shared_ptr<argus::ArgusWatcherSubject> subject);
    uint32_t getEventMaskFromSubject(std::shared_ptr<argus::ArgusWatcherSubject> subject);
    void createInotifyWatcher(std::string nodeName, std::string podName, std::shared_ptr<argus::ArgusWatcherSubject> subject,
        int pid, int sid, google::protobuf::RepeatedField<google::protobuf::int32> *procFds, mqd_t mq);
    mqd_t createMessageQueue(std::string logFormat, std::string name, std::string nodeName,
        std::string podName, google::protobuf::RepeatedPtrField<argus::ArgusWatcherSubject> subjects, mqd_t mq);
    static void startMessageQueue(std::string logFormat, std::string name, std::string nodeName,
        std::string podName, google::protobuf::RepeatedPtrField<argus::ArgusWatcherSubject> subjects,
        mqd_t mq, std::string mqPath);
    void sendKillSignalToWatcher(std::shared_ptr<argus::ArgusdHandle> watcher);
    void eraseEventProcessfd(google::protobuf::RepeatedField<google::protobuf::int32> *eventProcessfds, int processfd);
    void sendExitMessageToMessageQueue(std::shared_ptr<argus::ArgusdHandle> watcher);

    /**
     * Helper function to remove prepended container protocol from `containerId`
     * given a prefix; currently docker|cri-o|rkt|containerd.
     *
     * @param containerId
     * @param prefix
     */
    inline void cleanContainerId(std::string &containerId, const std::string &prefix) const {
        ArgusdUtil::eraseSubstr(containerId, prefix + "://");
    }

    std::vector<std::shared_ptr<argus::ArgusdHandle>> watchers_;
    static grpc::ServerWriter<argus::ArgusdMetricsHandle> *metricsWriter_;
    std::condition_variable cv_;
    std::mutex mux_;
};
} // namespace argusd

#endif
