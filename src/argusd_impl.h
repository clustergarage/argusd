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

#include <future>
#include <map>
#include <vector>

#include <argus-proto/c++/argus.grpc.pb.h>
#include <libcontainer/container_util.h>

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
    std::vector<int> getPidsFromRequest(std::shared_ptr<argus::ArgusdConfig> request) const;
    std::shared_ptr<argus::ArgusdHandle> findArgusdWatcherByPids(std::string nodeName, std::vector<int> pids) const;
    char **getPathArrayFromSubject(int pid, std::shared_ptr<argus::ArgusWatcherSubject> subject) const;
    char **getIgnoreArrayFromSubject(std::shared_ptr<argus::ArgusWatcherSubject> subject) const;
    std::string getTagListFromSubject(std::shared_ptr<argus::ArgusWatcherSubject> subject) const;
    uint32_t getEventMaskFromSubject(std::shared_ptr<argus::ArgusWatcherSubject> subject) const;
    uint32_t getFlagsFromSubject(std::shared_ptr<argus::ArgusWatcherSubject> subject) const;
    void createInotifyWatcher(std::string watcherName, std::string nodeName, std::string podName,
        std::shared_ptr<argus::ArgusWatcherSubject> subject, int pid, int sid, int slen,
        std::string logFormat);
    void sendKillSignalToWatcher(std::shared_ptr<argus::ArgusdHandle> watcher) const;

    /**
     * Helper function to remove prepended container protocol from `containerId`
     * given a prefix; currently docker|cri-o|rkt|containerd.
     *
     * @param containerId
     * @param prefix
     */
    inline void cleanContainerId(std::string &containerId, const std::string &prefix) const {
        clustergarage::container::Util::eraseSubstr(containerId, prefix + "://");
    }

    /**
     * Helper function to convert `str` as type `std::string` to a usable
     * C-style `char *`.
     *
     * @param str
     * @return
     */
    inline const char *convertStringToCString(const std::string &str) const {
        char *cstr = new char[str.size() + 1];
        strncpy(cstr, str.c_str(), str.size() + 1);
        return cstr;
    }

    std::vector<std::shared_ptr<argus::ArgusdHandle>> watchers_;
    std::map<int, bool> doneMap_;
    std::condition_variable cv_;
    std::mutex mux_;
};
} // namespace argusd

extern grpc::ServerWriter<argus::ArgusdMetricsHandle> *kMetricsWriter;

#ifdef __cplusplus
extern "C" {
#endif
const void logArgusWatchEvent(struct arguswatch_event *);
#ifdef __cplusplus
}; // extern "C"
#endif

#endif
