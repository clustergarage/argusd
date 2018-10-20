#include "fimd_impl.h"

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
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
/**
 * default logging format
 * specifiers that can be used:
 *   pod      name of the pod
 *   node     name of the node
 *   event    inotify event that was observed
 *   path     name of the directory path
 *   file     name of the file
 *   ftype    evaluates to "file" or "directory"
 *   sep      placeholder for a "/" character (e.g. between path/file)
 */
std::string FimdImpl::DEFAULT_FORMAT = "{event} {ftype} '{path}{sep}{file}' ({pod}:{node})";

/**
 * CreateWatch is responsible for creating (or updating) a fim watcher
 * find list of pids from the request's container ids list
 * with the list of pids, create inotify watchers by spawning a `fimnotify`
 * process that handles the filesystem-level instructions to do so
 * an mqueue process is created to watch on a pod-level mq file descriptor;
 * that way if this pod is killed all mqueue watchers go with it
 */
grpc::Status FimdImpl::CreateWatch(grpc::ServerContext *context, const fim::FimdConfig *request, fim::FimdHandle *response) {
    auto pids = getPidsFromRequest(std::make_shared<fim::FimdConfig>(*request));
    if (!pids.size()) {
        return grpc::Status::CANCELLED;
    }

    // find existing watcher by pid in case we need to update
    // inotify_add_watcher is designed to both add and modify depending
    // on if a fd exists already for this path
    auto watcher = findFimdWatcherByPids(request->nodename(), pids);
    LOG(INFO) << (watcher == nullptr ? "Starting" : "Updating") << " inotify watcher ("
        << request->podname() << ":" << request->nodename() << ")";
    if (watcher != nullptr) {
        // stop existing watcher polling
        sendKillSignalToWatcher(watcher);
        watcher->clear_processeventfd();
    }

    response->set_nodename(request->nodename().c_str());
    response->set_podname(request->podname().c_str());
    response->set_mqfd(static_cast<google::protobuf::int32>(createMessageQueue(request->logformat(),
        request->nodename(), request->podname(), (watcher != nullptr ? response->mqfd() : EOF))));

    for_each(pids.cbegin(), pids.cend(), [&](const int pid) {
        int i = 0;
        for_each(request->subject().cbegin(), request->subject().cend(), [&](const fim::FimWatcherSubject subject) {
            // @TODO: check if any watchers are started, if not, don't add to response
            createInotifyWatcher(std::make_shared<fim::FimWatcherSubject>(subject), pid, i,
                response->mutable_processeventfd(), response->mqfd());
            ++i;
        });
        response->add_pid(pid);
    });

    if (watcher == nullptr) {
        // store new watcher
        watchers_.push_back(std::make_shared<fim::FimdHandle>(*response));
    } else {
        std::for_each(response->processeventfd().cbegin(), response->processeventfd().cend(), [&](const int processfd) {
            watcher->add_processeventfd(processfd);
        });
    }

    return grpc::Status::OK;
}

/**
 * DestroyWatch is responsible for deleting a fim watcher
 * send exit message to the mqueue to stop watching for events to log
 * send kill signal to the `fimnotify` poller to stop that child process
 */
grpc::Status FimdImpl::DestroyWatch(grpc::ServerContext *context, const fim::FimdConfig *request, fim::Empty *response) {
    LOG(INFO) << "Stopping inotify watcher (" << request->podname() << ":" << request->nodename() << ")";

    auto watcher = findFimdWatcherByPids(request->nodename(), std::vector<int>(request->pid().cbegin(), request->pid().cend()));
    if (watcher != nullptr) {
        // stop existing message queue
        sendExitMessageToMessageQueue(watcher);
        // stop existing watcher polling
        sendKillSignalToWatcher(watcher);
    }
    watchers_.erase(remove(watchers_.begin(), watchers_.end(), watcher), watchers_.end());

    return grpc::Status::OK;
}

/**
 * GetWatchState periodically gets called by the Kubernetes controller and is
 * responsible for gathering the current watcher state to send back so the
 * controller can reconcile if any watchers need to be added or destroyed
 */
grpc::Status FimdImpl::GetWatchState(grpc::ServerContext *context, const fim::Empty *request, grpc::ServerWriter<fim::FimdHandle> *writer) {
    std::for_each(watchers_.cbegin(), watchers_.cend(), [&](const std::shared_ptr<fim::FimdHandle> watcher) {
        if (!writer->Write(*watcher)) {
            // broken stream
        }
    });
    return grpc::Status::OK;
}

/**
 * return list of pids looked up by container ids from request
 */
std::vector<int> FimdImpl::getPidsFromRequest(std::shared_ptr<fim::FimdConfig> request) {
    std::vector<int> pids;
    std::for_each(request->cid().cbegin(), request->cid().cend(), [&](const std::string cid) {
        int pid = FimdUtil::getPidForContainer(cleanContainerId(cid));
        if (pid) {
            pids.push_back(pid);
        }
    });
    return pids;
}

/**
 * returns stored watcher that pertains to a list of pids on a specific node
 */
std::shared_ptr<fim::FimdHandle> FimdImpl::findFimdWatcherByPids(const std::string nodeName, const std::vector<int> pids) {
    auto it = find_if(watchers_.cbegin(), watchers_.cend(), [&](std::shared_ptr<fim::FimdHandle> watcher) {
        bool foundPid;
        for (auto pid = pids.cbegin(); pid != pids.cend(); ++pid) {
            auto watcherPid = std::find_if(watcher->pid().cbegin(), watcher->pid().cend(),
                [&](int p) { return p == *pid; });
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
 * returns array of char buffer paths to do the actual watch on given a list
 * of subjects
 * these prepend /proc/{PID}/root on each path so we can monitor via profs
 * directly to receive inode events
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
 * returns array of char buffer paths to ignore given a lits of subjects
 * when doing a recursive watch, if ignore paths are provided that match a
 * specific path it will be skipped, including all its children
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
 * returns a bitwise-OR combined event mask given a subject
 * the subject->event can be an array of strings that match directly to an
 * inotify event
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
 * create child processes as background threads for spawning a fimnotify
 * watcher
 * we will create an anonymous pipe used to communicate to this background
 * thread later from this implementation; in the case of updating/deleting an
 * existing watcher
 * an additional cleanup thread is created to specify removing the anonymous
 * pipe in the case of an error returned by the fimnotify poller
 */
void FimdImpl::createInotifyWatcher(std::shared_ptr<fim::FimWatcherSubject> subject, const int pid, const int sid,
    google::protobuf::RepeatedField<google::protobuf::int32> *eventProcessfds, const mqd_t mq) {

    // create anonymous pipe to communicate with inotify watcher
    const int processfd = eventfd(0, EFD_CLOEXEC);
    if (processfd == EOF) {
        return;
    }
    eventProcessfds->Add(processfd);

    std::packaged_task<int(int, int, int, char **, int, char **, uint32_t, bool, bool, int, int, mqd_t)> task(start_inotify_watcher);
    std::shared_future<int> result(task.get_future());
    std::thread taskThread(std::move(task), pid, sid, subject->path_size(), getPathArrayFromSubject(pid, subject),
        subject->ignore_size(), getPathArrayFromIgnore(subject), getEventMaskFromSubject(subject),
        subject->onlydir(), subject->recursive(), subject->maxdepth(), processfd, mq);
    // start as daemon process
    taskThread.detach();

    // once the fimnotify task begins we listen for a return status in a
    // separate, cleanup thread
    // when this result comes back, if it's in error state, we do any necessary
    // cleanup here, such as destroy our anonymous pipe into the fimnotify
    // poller
    std::thread cleanupThread([=](std::shared_future<int> res) {
        std::future_status status;
        do {
            status = res.wait_for(std::chrono::seconds(1));
        } while (status != std::future_status::ready);

        if (res.valid() &&
            res.get() != EXIT_SUCCESS) {
            eraseEventProcessfd(eventProcessfds, processfd);
        }
    }, result);
    cleanupThread.detach();
}

/**
 * create child process as a background thread to listen on an mqueue file
 * descriptor that the fimnotify process will add events that need to be logged
 * out here
 */
mqd_t FimdImpl::createMessageQueue(const std::string logFormat, const std::string nodeName, const std::string podName, mqd_t mq) {
    std::stringstream ss;
    ss << MQ_QUEUE_NAME << "-" << podName;
    std::string mqPath = ss.str();

    if (mq != EOF) {
        mq_close(mq);
        mq_unlink(mqPath.c_str());
    }

    mq_attr attr;
    // initialize the queue attributes
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MQ_MAX_SIZE;
    attr.mq_curmsgs = 0;

    // create the message queue
    mq = mq_open(mqPath.c_str(), O_CREAT | O_CLOEXEC | O_RDWR, S_IRUSR | S_IWUSR, &attr);
    if (mq == EOF) {
#if DEBUG
        perror("mq_open");
#endif
        return EOF;
    }

    // start message queue
    std::packaged_task<void(const std::string, const std::string, const std::string, const mqd_t, const std::string)> queue(startMessageQueue);
    std::thread queueThread(move(queue), logFormat, nodeName, podName, mq, mqPath);
    // start as daemon process
    queueThread.detach();

    return mq;
}

/**
 * starts a blocking function that will be spawned as a background thread in
 * the above function
 * any message that comes through on the mqueue will be handled here and any
 * custom logging format can be applied to be processed here
 */
void FimdImpl::startMessageQueue(const std::string logFormat, const std::string nodeName, const std::string podName,
    const mqd_t mq, const std::string mqPath) {

    bool done = false;
    do {
        char buffer[MQ_MAX_SIZE + 1];
        ssize_t bytesRead = mq_receive(mq, buffer, MQ_MAX_SIZE, NULL);
        buffer[bytesRead] = '\0';
        if (bytesRead == EOF) {
            continue;
        }

        if (!strncmp(buffer, MQ_EXIT_MESSAGE, strlen(MQ_EXIT_MESSAGE))) {
            done = true;
        } else {
            fimwatch_event *fwevent = reinterpret_cast<struct fimwatch_event *>(buffer);
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

            fmt::memory_buffer out;
            try {
                fmt::format_to(out, logFormat != "" ? logFormat : FimdImpl::DEFAULT_FORMAT,
                    fmt::arg("event", maskStr),
                    fmt::arg("ftype", fwevent->is_dir ? "directory" : "file"),
                    fmt::arg("path", std::regex_replace(fwevent->path_name, procRegex, "")),
                    fmt::arg("file", fwevent->file_name),
                    fmt::arg("sep", fwevent->file_name != "" ? "/" : ""),
                    fmt::arg("pod", podName),
                    fmt::arg("node", nodeName));
                LOG(INFO) << fmt::to_string(out);
            } catch(const std::exception &e) {
                LOG(WARNING) << "Malformed FimWatcher `.spec.logFormat`: \"" << e.what() << "\"";
            }
        }
    } while (!done);

    mq_close(mq);
    mq_unlink(mqPath.c_str());
}

/**
 * sends a message over the anonymous pipe to stop the fimnotify poller
 */
void FimdImpl::sendKillSignalToWatcher(std::shared_ptr<fim::FimdHandle> watcher) {
    // kill existing watcher polls
    std::for_each(watcher->processeventfd().cbegin(), watcher->processeventfd().cend(), [&](const int processfd) {
        send_watcher_kill_signal(processfd);
        eraseEventProcessfd(watcher->mutable_processeventfd(), processfd);
    });
}

/**
 * shuts down the anonymous pipe used to communicate by the fimnotify poller
 * and removes it from the stored collection of pipes
 */
void FimdImpl::eraseEventProcessfd(google::protobuf::RepeatedField<google::protobuf::int32> *eventProcessfds, const int processfd) {
    if (!eventProcessfds->size()) {
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
 * sends a message to the mqueue file descriptor to stop the listener
 */
void FimdImpl::sendExitMessageToMessageQueue(std::shared_ptr<fim::FimdHandle> watcher) {
    // in order to stop the blocking mqueue process, send a specific exit
    // message that will break it out of the loop
    if (mq_send(watcher->mqfd(), MQ_EXIT_MESSAGE, strlen(MQ_EXIT_MESSAGE), 1) == EOF) {
#if DEBUG
        perror("mq_send");
#endif
    }
}
} // namespace fimd
