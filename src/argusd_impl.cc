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

#include "argusd_impl.h"

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <thread>

#include <fmt/format.h>
#include <glog/logging.h>
#include <grpc/grpc.h>
#include <grpc++/server_context.h>
#include <libcontainer/container_util.h>

extern "C" {
#include <lib/argusnotify.h>
#include <lib/argusutil.h>
}

grpc::ServerWriter<argus::ArgusdMetricsHandle> *kMetricsWriter;

namespace argusd {
/**
 * CreateWatch is responsible for creating (or updating) an argus watcher. Find
 * list of PIDs from the request's container IDs list. With the list of PIDs,
 * create `inotify` watchers by spawning an argusnotify process that handles
 * the filesystem-level instructions.
 *
 * @param context
 * @param request
 * @param response
 * @return
 */
grpc::Status ArgusdImpl::CreateWatch(grpc::ServerContext *context [[maybe_unused]], const argus::ArgusdConfig *request,
    argus::ArgusdHandle *response) {

    auto pids = getPidsFromRequest(std::make_shared<argus::ArgusdConfig>(*request));
    if (pids.empty()) {
        return grpc::Status::CANCELLED;
    }

    // Find existing watcher by pid in case we need to update
    // `inotify_add_watcher` is designed to both add and modify depending on if
    // a fd exists already for this path.
    auto watcher = findArgusdWatcherByPids(request->nodename(), pids);
    LOG(INFO) << (watcher == nullptr ? "Starting" : "Updating") << " `inotify` watcher ("
        << request->podname() << ":" << request->nodename() << ")";
    if (watcher != nullptr) {
        // Reset done flag.
        done_ = false;
        // Stop existing watcher polling.
        sendKillSignalToWatcher(watcher);

        // Wait for all inotify threads to be finished and cleaned up.
        std::unique_lock<std::mutex> lock(mux_);
        cv_.wait_until(lock, std::chrono::system_clock::now() + std::chrono::seconds(2), [=] {
            return done_;
        });
    }

    response->set_nodename(request->nodename().c_str());
    response->set_podname(request->podname().c_str());

    for_each(pids.cbegin(), pids.cend(), [&](const int pid) {
        int i = 0;
        for_each(request->subject().cbegin(), request->subject().cend(), [&](const argus::ArgusWatcherSubject subject) {
            // @TODO: Check if any watchers are started, if not, don't add to response.
            createInotifyWatcher(request->name(), response->nodename(), response->podname(),
                std::make_shared<argus::ArgusWatcherSubject>(subject), pid, i, request->subject_size(),
                request->logformat());
            ++i;
        });
        response->add_pid(pid);
    });

    if (watcher == nullptr) {
        // Store new watcher.
        watchers_.push_back(std::make_shared<argus::ArgusdHandle>(*response));
    }

    return grpc::Status::OK;
}

/**
 * DestroyWatch is responsible for deleting an argus watcher. Send kill signal
 * to the argusnotify poller to stop that child process.
 *
 * @param context
 * @param request
 * @param response
 * @return
 */
grpc::Status ArgusdImpl::DestroyWatch(grpc::ServerContext *context [[maybe_unused]], const argus::ArgusdConfig *request,
    argus::Empty *response [[maybe_unused]]) {

    LOG(INFO) << "Stopping `inotify` watcher (" << request->podname() << ":" << request->nodename() << ")";

    auto watcher = findArgusdWatcherByPids(request->nodename(), std::vector<int>(request->pid().cbegin(), request->pid().cend()));
    if (watcher != nullptr) {
        // Stop existing watcher polling.
        sendKillSignalToWatcher(watcher);
    }
    watchers_.erase(remove(watchers_.begin(), watchers_.end(), watcher), watchers_.end());

    return grpc::Status::OK;
}

/**
 * GetWatchState periodically gets called by the Kubernetes controller and is
 * responsible for gathering the current watcher state to send back so the
 * controller can reconcile if any watchers need to be added or destroyed.
 *
 * @param context
 * @param request
 * @param writer
 * @return
 */
grpc::Status ArgusdImpl::GetWatchState(grpc::ServerContext *context [[maybe_unused]], const argus::Empty *request [[maybe_unused]],
    grpc::ServerWriter<argus::ArgusdHandle> *writer) {

    std::for_each(watchers_.cbegin(), watchers_.cend(), [&](const std::shared_ptr<argus::ArgusdHandle> watcher) {
        if (!writer->Write(*watcher)) {
            // Broken stream.
        }
    });
    return grpc::Status::OK;
}

/**
 * RecordMetrics is used to send the controller `inotify` events that occur on
 * this daemon by way of a gRPC stream.
 *
 * @param context
 * @param request
 * @param writer
 * @return
 */
grpc::Status ArgusdImpl::RecordMetrics(grpc::ServerContext *context [[maybe_unused]], const argus::Empty *request [[maybe_unused]],
    grpc::ServerWriter<argus::ArgusdMetricsHandle> *writer) {

    kMetricsWriter = writer;

    std::condition_variable cv;
    std::mutex mux;
    std::unique_lock<std::mutex> lock(mux);
    // Keep alive so new events coming from argusnotify can be written to the
    // bidirectional gRPC stream.
    cv.wait(lock, [=] {
        return kMetricsWriter == nullptr;
    });

    return grpc::Status::OK;
}

/**
 * Return list of PIDs looked up by container IDs from request.
 *
 * @param request
 * @return
 */
std::vector<int> ArgusdImpl::getPidsFromRequest(std::shared_ptr<argus::ArgusdConfig> request) const {
    std::vector<int> pids;
    std::for_each(request->cid().cbegin(), request->cid().cend(), [&](std::string cid) {
        std::string runtime = clustergarage::container::Util::findContainerRuntime(cid);
        cleanContainerId(cid, runtime);
        int pid = clustergarage::container::Util::getPidForContainer(cid, runtime);
        if (pid) {
            pids.push_back(pid);
        }
    });
    return pids;
}

/**
 * Returns stored watcher that pertains to a list of PIDs on a specific node.
 *
 * @param nodeName
 * @param pids
 * @return
 */
std::shared_ptr<argus::ArgusdHandle> ArgusdImpl::findArgusdWatcherByPids(const std::string nodeName, const std::vector<int> pids) const {
    auto it = find_if(watchers_.cbegin(), watchers_.cend(), [&](std::shared_ptr<argus::ArgusdHandle> watcher) {
        bool foundPid = false;
        for (const auto &pid : pids) {
            auto watcherPid = std::find_if(watcher->pid().cbegin(), watcher->pid().cend(),
                [&](int p) { return p == pid; });
            foundPid = watcherPid != watcher->pid().cend();
        }
        return watcher->nodename() == nodeName && foundPid;
    });
    if (it != watchers_.cend()) {
        return *it;
    }
    return nullptr;
}

/**
 * Returns array of char buffer paths to do the actual watch on given a
 * subject. These prepend /proc/{PID}/root on each path so we can monitor via
 * profs directly to receive inode events.
 *
 * @param pid
 * @param subject
 * @return
 */
char **ArgusdImpl::getPathArrayFromSubject(const int pid, std::shared_ptr<argus::ArgusWatcherSubject> subject) const {
    std::vector<std::string> pathvec;
    std::for_each(subject->path().cbegin(), subject->path().cend(), [&](std::string path) {
        std::stringstream ss;
        ss << "/proc/" << pid << "/root" << path.c_str();
        pathvec.push_back(ss.str());
    });

    char **patharr = new char *[pathvec.size()];
    for(size_t i = 0; i < pathvec.size(); ++i) {
        patharr[i] = new char[pathvec[i].size() + 1];
        strcpy(patharr[i], pathvec[i].c_str());
    }
    return patharr;
}

/**
 * Returns array of char buffer paths to ignore given a subject. When doing a
 * recursive watch, if ignore paths are provided that match a specific path it
 * will be skipped, including all its children.
 *
 * @param subject
 * @return
 */
char **ArgusdImpl::getIgnoreArrayFromSubject(std::shared_ptr<argus::ArgusWatcherSubject> subject) const {
    char **patharr = new char *[subject->ignore_size()];
    size_t i = 0;
    std::for_each(subject->ignore().cbegin(), subject->ignore().cend(), [&](std::string path) {
        patharr[i] = new char[path.size() + 1];
        strcpy(patharr[i], path.c_str());
        ++i;
    });
    return patharr;
}

/**
 * Returns a comma-separated list of key=value pairs for a subject tag map.
 *
 * @param subject
 * @return
 */
std::string ArgusdImpl::getTagListFromSubject(std::shared_ptr<argus::ArgusWatcherSubject> subject) const {
    std::string tags;
    for (const auto &tag : subject->tags()) {
        if (!tags.empty()) {
            tags += ",";
        }
        tags += tag.first + "=" + tag.second;
    }
    return tags;
}

/**
 * Returns a bitwise-OR combined event mask given a subject. The subject->event
 * can be an array of strings that match directly to an `inotify` event.
 *
 * @param subject
 * @return
 */
uint32_t ArgusdImpl::getEventMaskFromSubject(std::shared_ptr<argus::ArgusWatcherSubject> subject) const {
    uint32_t mask = 0;
    std::for_each(subject->event().cbegin(), subject->event().cend(), [&](std::string event) {
        const char *evt = event.c_str();
        if (strcmp(evt, "all") == 0)               mask |= IN_ALL_EVENTS;
        else if (strcmp(evt, "access") == 0)       mask |= IN_ACCESS;
        else if (strcmp(evt, "attrib") == 0)       mask |= IN_ATTRIB;
        else if (strcmp(evt, "closewrite") == 0)   mask |= IN_CLOSE_WRITE;
        else if (strcmp(evt, "closenowrite") == 0) mask |= IN_CLOSE_NOWRITE;
        else if (strcmp(evt, "close") == 0)        mask |= IN_CLOSE;
        else if (strcmp(evt, "create") == 0)       mask |= IN_CREATE;
        else if (strcmp(evt, "delete") == 0)       mask |= IN_DELETE;
        else if (strcmp(evt, "deleteself") == 0)   mask |= IN_DELETE_SELF;
        else if (strcmp(evt, "modify") == 0)       mask |= IN_MODIFY;
        else if (strcmp(evt, "moveself") == 0)     mask |= IN_MOVE_SELF;
        else if (strcmp(evt, "movedfrom") == 0)    mask |= IN_MOVED_FROM;
        else if (strcmp(evt, "movedto") == 0)      mask |= IN_MOVED_TO;
        else if (strcmp(evt, "move") == 0)         mask |= IN_MOVE;
        else if (strcmp(evt, "open") == 0)         mask |= IN_OPEN;
    });
    return mask;
}

/**
 * Create child processes as background threads for spawning an argusnotify
 * watcher. We will create an anonymous pipe used to communicate to this
 * background thread later from this implementation; in the case of
 * updating/deleting an existing watcher. An additional cleanup thread is
 * created to specify removing the anonymous pipe in the case of an error
 * returned by the argusnotify poller.
 *
 * @param watcherName
 * @param nodeName
 * @param podName
 * @param subject
 * @param pid
 * @param sid
 * @param subjectLen
 * @param logFormat
 */
void ArgusdImpl::createInotifyWatcher(const std::string watcherName, const std::string nodeName, const std::string podName,
    std::shared_ptr<argus::ArgusWatcherSubject> subject, const int pid, const int sid, const int subjectLen,
    const std::string logFormat) {

    std::packaged_task<int(const char *, const char *, const char *, int, int, unsigned int, const char **,
        unsigned int, const char **, uint32_t, bool, bool, int, bool, const char *, const char *,
        arguswatch_logfn)> task(start_inotify_watcher);
    std::shared_future<int> result(task.get_future());
    std::thread taskThread(std::move(task),
        convertStringToCString(watcherName),
        convertStringToCString(nodeName),
        convertStringToCString(podName),
        pid, sid,
        subject->path_size(), const_cast<const char **>(getPathArrayFromSubject(pid, subject)),
        subject->ignore_size(), const_cast<const char **>(getIgnoreArrayFromSubject(subject)),
        getEventMaskFromSubject(subject),
        subject->onlydir(),
        subject->recursive(), subject->maxdepth(),
        subject->followmove(),
        convertStringToCString(getTagListFromSubject(subject)),
        convertStringToCString(logFormat),
        logArgusWatchEvent);
    // Start as daemon process.
    taskThread.detach();

    // Once the argusnotify task begins we listen for a return status in a
    // separate, cleanup thread. When this result comes back, we do any
    // necessary cleanup here, such as destroy our anonymous pipe into the
    // argusnotify poller.
    int cnt = 0;
    std::thread cleanupThread([=](std::shared_future<int> res) mutable {
        res.wait();
        if (res.valid()) {
            if (++cnt == subjectLen) {
                done_ = true;
            }
            // Notify the `condition_variable` of changes.
            cv_.notify_one();
        }
    }, result);
    cleanupThread.detach();
}

/**
 * Sends a message over the anonymous pipe to stop the argusnotify poller.
 *
 * @param watcher
 */
void ArgusdImpl::sendKillSignalToWatcher(std::shared_ptr<argus::ArgusdHandle> watcher) const {
    // Kill existing watcher polls.
    std::for_each(watcher->pid().cbegin(), watcher->pid().cend(), [&](const int pid) {
        send_watcher_kill_signal(pid);
    });
}
} // namespace argusd

#ifdef __cplusplus
extern "C" {
#endif
const void logArgusWatchEvent(struct arguswatch_event *awevent) {
    /**
     * Default logging format.
     *
     * @specifier pod      Name of the pod.
     * @specifier node     Name of the node.
     * @specifier event    `inotify` event that was observed.
     * @specifier path     Name of the directory path.
     * @specifier file     Name of the file.
     * @specifier ftype    Evaluates to "file" or "directory".
     * @specifier tags     List of custom tags in key=value comma-separated list.
     * @specifier sep      Placeholder for a "/" character (e.g. between path/file).
     */
    static const std::string kDefaultFormat = "{event} {ftype} '{path}{sep}{file}' ({pod}:{node}) {tags}";

    std::string maskStr;
    if (awevent->event_mask & IN_ACCESS)             maskStr = "ACCESS";
    else if (awevent->event_mask & IN_ATTRIB)        maskStr = "ATTRIB";
    else if (awevent->event_mask & IN_CLOSE_WRITE)   maskStr = "CLOSE_WRITE";
    else if (awevent->event_mask & IN_CLOSE_NOWRITE) maskStr = "CLOSE_NOWRITE";
    else if (awevent->event_mask & IN_CREATE)        maskStr = "CREATE";
    else if (awevent->event_mask & IN_DELETE)        maskStr = "DELETE";
    else if (awevent->event_mask & IN_DELETE_SELF)   maskStr = "DELETE_SELF";
    else if (awevent->event_mask & IN_MODIFY)        maskStr = "MODIFY";
    else if (awevent->event_mask & IN_MOVE_SELF)     maskStr = "MOVE_SELF";
    else if (awevent->event_mask & IN_MOVED_FROM)    maskStr = "MOVED_FROM";
    else if (awevent->event_mask & IN_MOVED_TO)      maskStr = "MOVED_TO";
    else if (awevent->event_mask & IN_OPEN)          maskStr = "OPEN";

    fmt::memory_buffer out;
    try {
        fmt::format_to(out, *awevent->watch->log_format ? std::string(awevent->watch->log_format) : kDefaultFormat,
            fmt::arg("event", maskStr),
            fmt::arg("ftype", awevent->is_dir ? "directory" : "file"),
            fmt::arg("path", std::regex_replace(awevent->path_name, std::regex("/proc/[0-9]+/root"), "")),
            fmt::arg("file", awevent->file_name),
            fmt::arg("sep", *awevent->file_name ? "/" : ""),
            fmt::arg("pod", awevent->watch->pod_name),
            fmt::arg("node", awevent->watch->node_name),
            fmt::arg("tags", *awevent->watch->tags ? awevent->watch->tags : ""));
        LOG(INFO) << fmt::to_string(out);
    } catch(const std::exception &e) {
        LOG(WARNING) << "Malformed ArgusWatcher `.spec.logFormat`: \"" << e.what() << "\"";
    }

    if (kMetricsWriter != nullptr) {
        auto metric = std::make_shared<argus::ArgusdMetricsHandle>();
        metric->set_arguswatcher(awevent->watch->name);
        std::transform(maskStr.begin(), maskStr.end(), maskStr.begin(), ::tolower);
        metric->set_event(maskStr);
        metric->set_nodename(awevent->watch->node_name);
        // Record event to metrics writer to be put into Prometheus.
        if (!kMetricsWriter->Write(*metric)) {
            // Broken stream.
        }
    }
}
#ifdef __cplusplus
}; // extern "C"
#endif
