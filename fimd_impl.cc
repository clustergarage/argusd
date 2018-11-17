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

#include "fimd_impl.h"

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <thread>

#include <fmt/format.h>
#include <glog/logging.h>
#include <grpc/grpc.h>
#include <grpc++/server_context.h>

#include "fimd_util.h"
extern "C" {
#include "lib/fimnotify.h"
#include "lib/fimutil.h"
}

namespace fimd {
grpc::ServerWriter<fim::FimdMetricsHandle> *FimdImpl::metricsWriter_;

/**
 * CreateWatch is responsible for creating (or updating) a fim watcher. Find
 * list of PIDs from the request's container IDs list. With the list of PIDs,
 * create `inotify` watchers by spawning a fimnotify process that handles the
 * filesystem-level instructions to do so an mqueue process is created to watch
 * on a pod-level mq file descriptor. That way if this pod is killed all mqueue
 * watchers go with it.
 *
 * @param context
 * @param request
 * @param response
 * @return
 */
grpc::Status FimdImpl::CreateWatch(grpc::ServerContext *context [[maybe_unused]], const fim::FimdConfig *request,
    fim::FimdHandle *response) {

    auto pids = getPidsFromRequest(std::make_shared<fim::FimdConfig>(*request));
    if (pids.empty()) {
        return grpc::Status::CANCELLED;
    }

    // Find existing watcher by pid in case we need to update
    // `inotify_add_watcher` is designed to both add and modify depending on if
    // a fd exists already for this path.
    auto watcher = findFimdWatcherByPids(request->nodename(), pids);
    LOG(INFO) << (watcher == nullptr ? "Starting" : "Updating") << " `inotify` watcher ("
        << request->podname() << ":" << request->nodename() << ")";
    if (watcher != nullptr) {
        // Stop existing message queue.
        sendExitMessageToMessageQueue(watcher);
        // Stop existing watcher polling.
        sendKillSignalToWatcher(watcher);

        // Wait for all processeventfd to be cleared. This indicates that the
        // inotify threads are finished and cleaned up.
        for (;;) {
            if (!watcher->processeventfd_size()) {
                break;
            }
        }
    }

    response->set_nodename(request->nodename().c_str());
    response->set_podname(request->podname().c_str());
    response->set_mqfd(static_cast<google::protobuf::int32>(createMessageQueue(request->logformat(),
        request->name(), request->nodename(), request->podname(), request->subject(),
        (watcher != nullptr ? response->mqfd() : EOF))));

    for_each(pids.cbegin(), pids.cend(), [&](const int pid) {
        int i = 0;
        for_each(request->subject().cbegin(), request->subject().cend(), [&](const fim::FimWatcherSubject subject) {
            // @TODO: Check if any watchers are started, if not, don't add to response.
            createInotifyWatcher(response->nodename(), response->podname(), std::make_shared<fim::FimWatcherSubject>(subject),
                pid, i, response->mutable_processeventfd(), response->mqfd());
            ++i;
        });
        response->add_pid(pid);
    });

    if (watcher == nullptr) {
        // Store new watcher.
        watchers_.push_back(std::make_shared<fim::FimdHandle>(*response));
    } else {
        std::for_each(response->processeventfd().cbegin(), response->processeventfd().cend(), [&](const int processfd) {
            watcher->add_processeventfd(processfd);
        });
    }

    return grpc::Status::OK;
}

/**
 * DestroyWatch is responsible for deleting a fim watcher. Send exit message to
 * the mqueue to stop watching for events to log. Send kill signal to the
 * fimnotify poller to stop that child process.
 *
 * @param context
 * @param request
 * @param response
 * @return
 */
grpc::Status FimdImpl::DestroyWatch(grpc::ServerContext *context [[maybe_unused]], const fim::FimdConfig *request,
    fim::Empty *response [[maybe_unused]]) {

    LOG(INFO) << "Stopping `inotify` watcher (" << request->podname() << ":" << request->nodename() << ")";

    auto watcher = findFimdWatcherByPids(request->nodename(), std::vector<int>(request->pid().cbegin(), request->pid().cend()));
    if (watcher != nullptr) {
        // Stop existing message queue.
        sendExitMessageToMessageQueue(watcher);
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
grpc::Status FimdImpl::GetWatchState(grpc::ServerContext *context [[maybe_unused]], const fim::Empty *request [[maybe_unused]],
    grpc::ServerWriter<fim::FimdHandle> *writer) {

    std::for_each(watchers_.cbegin(), watchers_.cend(), [&](const std::shared_ptr<fim::FimdHandle> watcher) {
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
grpc::Status FimdImpl::RecordMetrics(grpc::ServerContext *context [[maybe_unused]], const fim::Empty *request [[maybe_unused]],
    grpc::ServerWriter<fim::FimdMetricsHandle> *writer) {

    metricsWriter_ = writer;
    for (;;) {
        if (metricsWriter_ == nullptr) {
            break;
        }
        // Keep alive so the mq streams in new events as it gets them.
    }
    return grpc::Status::OK;
}

/**
 * Return list of PIDs looked up by container IDs from request.
 *
 * @param request
 * @return
 */
std::vector<int> FimdImpl::getPidsFromRequest(std::shared_ptr<fim::FimdConfig> request) {
    std::vector<int> pids;
    std::for_each(request->cid().cbegin(), request->cid().cend(), [&](std::string cid) {
        std::string runtime = FimdUtil::findContainerRuntime(cid);
        cleanContainerId(cid, runtime);
        int pid = FimdUtil::getPidForContainer(cid, runtime);
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
std::shared_ptr<fim::FimdHandle> FimdImpl::findFimdWatcherByPids(const std::string nodeName, const std::vector<int> pids) {
    auto it = find_if(watchers_.cbegin(), watchers_.cend(), [&](std::shared_ptr<fim::FimdHandle> watcher) {
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
 * Returns array of char buffer paths to do the actual watch on given a list of
 * subjects. These prepend /proc/{PID}/root on each path so we can monitor via profs
 * directly to receive inode events.
 *
 * @param pid
 * @param subject
 * @return
 */
char **FimdImpl::getPathArrayFromSubject(const int pid, std::shared_ptr<fim::FimWatcherSubject> subject) {
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
 * Returns array of char buffer paths to ignore given a list of subjects. When
 * doing a recursive watch, if ignore paths are provided that match a specific
 * path it will be skipped, including all its children.
 *
 * @param subject
 * @return
 */
char **FimdImpl::getPathArrayFromIgnore(std::shared_ptr<fim::FimWatcherSubject> subject) {
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
std::string FimdImpl::getTagListFromSubject(std::shared_ptr<fim::FimWatcherSubject> subject) {
    std::string tags;
    for (auto tag : subject->tags()) {
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
uint32_t FimdImpl::getEventMaskFromSubject(std::shared_ptr<fim::FimWatcherSubject> subject) {
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
 * Create child processes as background threads for spawning a fimnotify
 * watcher. We will create an anonymous pipe used to communicate to this
 * background thread later from this implementation; in the case of
 * updating/deleting an existing watcher. An additional cleanup thread is
 * created to specify removing the anonymous pipe in the case of an error
 * returned by the fimnotify poller.
 *
 * @param nodeName
 * @param podName
 * @param subject
 * @param pid
 * @param sid
 * @param eventProcessfds
 * @param mq
 */
void FimdImpl::createInotifyWatcher(const std::string nodeName, const std::string podName,
    std::shared_ptr<fim::FimWatcherSubject> subject, const int pid, const int sid,
    google::protobuf::RepeatedField<google::protobuf::int32> *eventProcessfds, const mqd_t mq) {

    // Create anonymous pipe to communicate with `inotify` watcher.
    const int processfd = eventfd(0, EFD_CLOEXEC);
    if (processfd == EOF) {
        return;
    }
    eventProcessfds->Add(processfd);

    char **subjectPaths = getPathArrayFromSubject(pid, subject);
    char **ignorePaths = getPathArrayFromIgnore(subject);

    std::packaged_task<int(int, int, unsigned int, char **, unsigned int, char **, uint32_t,
        bool, bool, int, int, mqd_t)> task(start_inotify_watcher);
    std::shared_future<int> result(task.get_future());
    std::thread taskThread(std::move(task), pid, sid, subject->path_size(), subjectPaths/*getPathArrayFromSubject(pid, subject)*/,
        subject->ignore_size(), ignorePaths/*getPathArrayFromIgnore(subject)*/, getEventMaskFromSubject(subject),
        subject->onlydir(), subject->recursive(), subject->maxdepth(), processfd, mq);
    // Start as daemon process.
    taskThread.detach();

    // Once the fimnotify task begins we listen for a return status in a
    // separate, cleanup thread. When this result comes back, we do any
    // necessary cleanup here, such as destroy our anonymous pipe into the
    // fimnotify poller.
    std::thread cleanupThread([=](std::shared_future<int> res) mutable {
        res.wait();
        if (res.valid()) {
            auto watcher = findFimdWatcherByPids(nodeName, std::vector<int>{pid});
            if (watcher != nullptr) {
                eraseEventProcessfd(watcher->mutable_processeventfd(), processfd);
            }
            delete[] subjectPaths;
            delete[] ignorePaths;
        }
    }, result);
    cleanupThread.detach();
}

/**
 * Create child process as a background thread to listen on an mqueue file
 * descriptor that the fimnotify process will add events that need to be logged
 * out here.
 *
 * @param logFormat
 * @param name
 * @param nodeName
 * @param podName
 * @param subjects
 * @param mq
 * @return
 */
mqd_t FimdImpl::createMessageQueue(const std::string logFormat, const std::string name, const std::string nodeName,
    const std::string podName, const google::protobuf::RepeatedPtrField<fim::FimWatcherSubject> subjects, mqd_t mq) {

    std::stringstream ss;
    ss << MQ_QUEUE_NAME << "-" << podName;
    std::string mqPath = ss.str();

    if (mq != EOF) {
        mq_close(mq);
        mq_unlink(mqPath.c_str());
    }

    // Initialize the queue attributes.
    mq_attr attr = {
        .mq_flags = 0,
        .mq_maxmsg = 10,
        .mq_msgsize = MQ_MAX_SIZE,
        .mq_curmsgs = 0
    };

    // Create the message queue.
    mq = mq_open(mqPath.c_str(), O_CREAT | O_CLOEXEC | O_RDWR, S_IRUSR | S_IWUSR, &attr);
    if (mq == EOF) {
#if DEBUG
        perror("mq_open");
#endif
        return EOF;
    }

    // Start message queue.
    std::packaged_task<void(const std::string, const std::string, const std::string, const std::string,
        const google::protobuf::RepeatedPtrField<fim::FimWatcherSubject>,
        const mqd_t, const std::string)> queue(startMessageQueue);
    std::thread queueThread(move(queue), logFormat, name, nodeName, podName, subjects, mq, mqPath);
    // Start as daemon process.
    queueThread.detach();

    return mq;
}

/**
 * Starts a blocking function that will be spawned as a background thread in
 * the above function. Any message that comes through on the mqueue will be
 * handled here and any custom logging format can be applied to be processed
 * here.
 *
 * @param logFormat
 * @param name
 * @param nodeName
 * @param podName
 * @param subjects
 * @param mq
 * @param mqPath
 */
void FimdImpl::startMessageQueue(const std::string logFormat, const std::string name, const std::string nodeName,
    const std::string podName, const google::protobuf::RepeatedPtrField<fim::FimWatcherSubject> subjects,
    const mqd_t mq, const std::string mqPath) {

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
    std::string DEFAULT_FORMAT = "{event} {ftype} '{path}{sep}{file}' ({pod}:{node}) {tags}";

    bool done = false;
    do {
        char buffer[MQ_MAX_SIZE + 1];
        ssize_t bytesRead = mq_receive(mq, buffer, MQ_MAX_SIZE, nullptr);
        buffer[bytesRead] = '\0';
        if (bytesRead == EOF) {
            continue;
        }

        if (!strncmp(buffer, MQ_EXIT_MESSAGE, strlen(MQ_EXIT_MESSAGE))) {
            done = true;
        } else {
            auto fwevent = reinterpret_cast<struct fimwatch_event *>(buffer);
            std::regex procRegex("/proc/[0-9]+/root");

            std::string maskStr;
            if (fwevent->event_mask & IN_ACCESS)             maskStr = "ACCESS";
            else if (fwevent->event_mask & IN_ATTRIB)        maskStr = "ATTRIB";
            else if (fwevent->event_mask & IN_CLOSE_WRITE)   maskStr = "CLOSE_WRITE";
            else if (fwevent->event_mask & IN_CLOSE_NOWRITE) maskStr = "CLOSE_NOWRITE";
            else if (fwevent->event_mask & IN_CREATE)        maskStr = "CREATE";
            else if (fwevent->event_mask & IN_DELETE)        maskStr = "DELETE";
            else if (fwevent->event_mask & IN_DELETE_SELF)   maskStr = "DELETE_SELF";
            else if (fwevent->event_mask & IN_MODIFY)        maskStr = "MODIFY";
            else if (fwevent->event_mask & IN_MOVE_SELF)     maskStr = "MOVE_SELF";
            else if (fwevent->event_mask & IN_MOVED_FROM)    maskStr = "MOVED_FROM";
            else if (fwevent->event_mask & IN_MOVED_TO)      maskStr = "MOVED_TO";
            else if (fwevent->event_mask & IN_OPEN)          maskStr = "OPEN";

            const auto subject = std::make_shared<fim::FimWatcherSubject>(subjects.Get(fwevent->sid));

            fmt::memory_buffer out;
            try {
                fmt::format_to(out, !logFormat.empty() ? logFormat : DEFAULT_FORMAT,
                    fmt::arg("event", maskStr),
                    fmt::arg("ftype", fwevent->is_dir ? "directory" : "file"),
                    fmt::arg("path", std::regex_replace(fwevent->path_name, procRegex, "")),
                    fmt::arg("file", fwevent->file_name),
                    fmt::arg("sep", *fwevent->file_name ? "/" : ""),
                    fmt::arg("pod", podName),
                    fmt::arg("node", nodeName),
                    fmt::arg("tags", subject != nullptr ? getTagListFromSubject(subject) : ""));
                LOG(INFO) << fmt::to_string(out);
            } catch(const std::exception &e) {
                LOG(WARNING) << "Malformed FimWatcher `.spec.logFormat`: \"" << e.what() << "\"";
            }

            if (metricsWriter_ != nullptr) {
                auto metric = std::make_shared<fim::FimdMetricsHandle>();
                metric->set_fimwatcher(name);
                std::transform(maskStr.begin(), maskStr.end(), maskStr.begin(), ::tolower);
                metric->set_event(maskStr);
                metric->set_nodename(nodeName);
                // Record event to metrics writer to be put into Prometheus.
                if (!metricsWriter_->Write(*metric)) {
                    // Broken stream.
                }
            }
        }
    } while (!done);

    mq_close(mq);
    mq_unlink(mqPath.c_str());
}

/**
 * Sends a message over the anonymous pipe to stop the fimnotify poller.
 *
 * @param watcher
 */
void FimdImpl::sendKillSignalToWatcher(std::shared_ptr<fim::FimdHandle> watcher) {
    // Kill existing watcher polls.
    std::for_each(watcher->processeventfd().cbegin(), watcher->processeventfd().cend(), [&](const int processfd) {
        send_watcher_kill_signal(processfd);
    });
}

/**
 * Shuts down the anonymous pipe used to communicate by the fimnotify poller
 * and removes it from the stored collection of pipes.
 *
 * @param eventProcessfds
 * @param processfd
 */
void FimdImpl::eraseEventProcessfd(google::protobuf::RepeatedField<google::protobuf::int32> *eventProcessfds, const int processfd) {
    if (eventProcessfds->empty()) {
       return;
    }
    for (auto it = eventProcessfds->cbegin(); it != eventProcessfds->cend(); ++it) {
       if (*it == processfd) {
           eventProcessfds->erase(it);
           break;
       }
    }
}

/**
 * Sends a message to the mqueue file descriptor to stop the listener.
 *
 * @param watcher
 */
void FimdImpl::sendExitMessageToMessageQueue(std::shared_ptr<fim::FimdHandle> watcher) {
    // In order to stop the blocking mqueue process, send a specific exit
    // message that will break it out of the loop.
    if (mq_send(watcher->mqfd(), MQ_EXIT_MESSAGE, strlen(MQ_EXIT_MESSAGE), 1) == EOF) {
#if DEBUG
        perror("mq_send");
#endif
    }
}
} // namespace fimd
