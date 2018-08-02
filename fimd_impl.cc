#include "fimd_impl.h"

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <future>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include <glog/logging.h>
#include <grpc/grpc.h>
#include <grpc++/server_context.h>

#include "fimd_util.h"
extern "C" {
#include "lib/fimnotify.h"
}

namespace fimd {
grpc::Status FimdImpl::CreateWatch(grpc::ServerContext *context, const fim::FimdConfig *request, fim::FimdHandle *response) {
    auto pids = getPidsFromRequest(request);
    if (!pids.size()) {
        return grpc::Status::CANCELLED;
    }

    // find existing watcher by pid in case we need to update
    // inotify_add_watcher is designed to both add and modify depending
    // on if a fd exists already for this path
    auto watcher = findFimdWatcherByPids(request->hostuid(), pids);
    if (watcher == nullptr) {
        LOG(INFO) << "Starting inotify watcher...";
    } else {
        LOG(INFO) << "Updating inotify watcher...";

        // stop existing message queue
        sendExitMessageToMessageQueue(watcher);
        // stop existing watcher polling
        sendKillSignalToWatcher(watcher);
        watcher->clear_processeventfd();
    }

    response->set_hostuid(request->hostuid().c_str());
    response->set_mqfd(static_cast<google::protobuf::int32>(createMessageQueue()));

    for_each(pids.cbegin(), pids.cend(), [&](const int pid) {
        for_each(request->subject().cbegin(), request->subject().cend(), [&](const fim::FimWatcherSubject subject) {
            // @TODO: check if any watchers are started, if not, don't add to response
            createInotifyWatcher(subject, getPathArrayFromSubject(pid, subject),
                getEventMaskFromSubject(subject), response->mutable_processeventfd());
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

grpc::Status FimdImpl::DestroyWatch(grpc::ServerContext *context, const fim::FimdConfig *request, fim::Empty *response) {
    auto pids = getPidsFromRequest(request);
    if (!pids.size()) {
        return grpc::Status::CANCELLED;
    }

    LOG(INFO) << "Stopping inotify watcher...";

    auto watcher = findFimdWatcherByPids(request->hostuid(), pids);
    if (watcher != nullptr) {
        // stop existing message queue
        sendExitMessageToMessageQueue(watcher);
        // stop existing watcher polling
        sendKillSignalToWatcher(watcher);
    }
    watchers_.erase(remove(watchers_.begin(), watchers_.end(), watcher), watchers_.end());

    return grpc::Status::OK;
}

std::vector<int> FimdImpl::getPidsFromRequest(const fim::FimdConfig *request) {
    std::vector<int> pids;
    std::for_each(request->containerid().cbegin(), request->containerid().cend(), [&](const std::string containerId) {
        int pid = FimdUtil::getPidForContainer(cleanContainerId(containerId));
        if (pid) {
            pids.push_back(pid);
        }
    });
    return pids;
}

std::shared_ptr<fim::FimdHandle> FimdImpl::findFimdWatcherByPids(const std::string hostUid, const std::vector<int> pids) {
    auto it = find_if(watchers_.cbegin(), watchers_.cend(), [&](std::shared_ptr<fim::FimdHandle> watcher) {
        bool foundPid;
        for (auto pid = pids.cbegin(); pid != pids.cend(); ++pid) {
            auto watcherPid = std::find_if(watcher->pid().cbegin(), watcher->pid().cend(),
                [&](int p) { return p == *pid; });
            foundPid = watcherPid != watcher->pid().cend();
        }
        return watcher->hostuid() == hostUid && foundPid;
    });
    if (it != watchers_.cend()) {
        return *it;
    }
    return nullptr;
}

char **FimdImpl::getPathArrayFromSubject(const int pid, const fim::FimWatcherSubject subject) {
    std::vector<std::string> pathvec;
    std::for_each(subject.path().cbegin(), subject.path().cend(), [&](std::string path) {
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

uint32_t FimdImpl::getEventMaskFromSubject(const fim::FimWatcherSubject subject) {
    // @TODO: document this - also: IN_EXCL_UNLINK
    uint32_t mask = 0; //IN_DONT_FOLLOW;
    std::for_each(subject.event().cbegin(), subject.event().cend(), [&](std::string event) {
        const char *evt = event.c_str();
        if (strcmp(evt, "all") == 0)         mask |= IN_ALL_EVENTS;
        else if (strcmp(evt, "access") == 0) mask |= IN_ACCESS;
        else if (strcmp(evt, "modify") == 0) mask |= IN_MODIFY;
        else if (strcmp(evt, "attrib") == 0) mask |= IN_ATTRIB;
        else if (strcmp(evt, "open") == 0)   mask |= IN_OPEN;
        else if (strcmp(evt, "close") == 0)  mask |= IN_CLOSE;
        else if (strcmp(evt, "create") == 0) mask |= IN_CREATE;
        else if (strcmp(evt, "delete") == 0) mask |= IN_DELETE;
        else if (strcmp(evt, "move") == 0)   mask |= IN_MOVE;
    });
    return mask;
}

void FimdImpl::createInotifyWatcher(const fim::FimWatcherSubject subject, char **patharr, uint32_t event_mask,
    google::protobuf::RepeatedField<google::protobuf::int32> *eventProcessfds) {
    // create anonymous pipe to communicate with inotify watcher
    int processfd = eventfd(0, EFD_CLOEXEC);
    if (processfd == EOF) {
        return;
    }
    eventProcessfds->Add(processfd);

    std::packaged_task<int(int, char **, uint32_t, int)> task(start_inotify_watcher);
    std::future<int> result = task.get_future();

    std::thread taskThread(std::move(task), subject.path_size(), static_cast<char **>(patharr),
        static_cast<uint32_t>(event_mask), processfd);
    // start as daemon process
    taskThread.detach();

    // @TODO: re-evaluate this timeout
    std::future_status status = result.wait_for(std::chrono::milliseconds(100));
    if (status == std::future_status::ready &&
        result.get() != EXIT_SUCCESS) {
        eraseEventProcessfd(eventProcessfds, processfd);
    }
}

mqd_t FimdImpl::createMessageQueue() {
    mqd_t mq;
    mq_attr attr;
    // initialize the queue attributes
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MQ_MAX_SIZE;
    attr.mq_curmsgs = 0;

    // create the message queue
    mq = mq_open(MQ_QUEUE_NAME, O_CREAT | O_RDWR, 0644, &attr);
    if (mq == EOF) {
#if DEBUG
        perror("mq_open");
#endif
    }

    // start message queue
    std::packaged_task<void(mqd_t)> queue(startMessageQueue);
    std::thread queueThread(move(queue), mq);
    // start as daemon process
    queueThread.detach();

    return mq;
}

void FimdImpl::startMessageQueue(mqd_t mq) {
    bool done = false;
    do {
        char buffer[MQ_MAX_SIZE + 1];
        ssize_t bytes_read = mq_receive(mq, buffer, MQ_MAX_SIZE, NULL);
        buffer[bytes_read] = '\0';

        if (!strncmp(buffer, MQ_EXIT_MESSAGE, strlen(MQ_EXIT_MESSAGE))) {
            done = true;
        } else {
            fimwatch_event *fwevent = reinterpret_cast<struct fimwatch_event *>(buffer);
            std::stringstream ss;

            if (fwevent->event_mask & IN_ACCESS)             ss << "IN_ACCESS";
            else if (fwevent->event_mask & IN_MODIFY)        ss << "IN_MODIFY";
            else if (fwevent->event_mask & IN_ATTRIB)        ss << "IN_ATTRIB";
            else if (fwevent->event_mask & IN_OPEN)          ss << "IN_OPEN";
            else if (fwevent->event_mask & IN_CLOSE_WRITE)   ss << "IN_CLOSE_WRITE";
            else if (fwevent->event_mask & IN_CLOSE_NOWRITE) ss << "IN_CLOSE_NOWRITE";
            else if (fwevent->event_mask & IN_CREATE)        ss << "IN_CREATE";
            else if (fwevent->event_mask & IN_DELETE)        ss << "IN_DELETE";
            else if (fwevent->event_mask & IN_DELETE_SELF)   ss << "IN_DELETE_SELF";
            else if (fwevent->event_mask & IN_MOVED_FROM)    ss << "IN_MOVED_FROM";
            else if (fwevent->event_mask & IN_MOVED_TO)      ss << "IN_MOVED_TO";
            else if (fwevent->event_mask & IN_MOVE_SELF)     ss << "IN_MOVE_SELF";

            // @TODO: replace /proc/$PID/root for log-readability
            ss << ": " << fwevent->path_name << "/" << fwevent->file_name <<
                " [" << (fwevent->is_dir & IN_ISDIR ? "directory" : "file") << "]";
            LOG(INFO) << ss.str();
        }
    } while (!done);

    mq_close(mq);
    mq_unlink(MQ_QUEUE_NAME);
}

void FimdImpl::sendKillSignalToWatcher(std::shared_ptr<fim::FimdHandle> watcher) {
    // kill existing watcher polls
    uint64_t value = FIMNOTIFY_KILL;
    std::for_each(watcher->processeventfd().cbegin(), watcher->processeventfd().cend(), [&](const int processfd) {
        if (write(processfd, &value, sizeof(value)) == EOF) {
            // do stuff
        }
        eraseEventProcessfd(watcher->mutable_processeventfd(), processfd);
    });
}

void FimdImpl::eraseEventProcessfd(google::protobuf::RepeatedField<google::protobuf::int32> *eventProcessfds, const int processfd) {
    for (auto it = eventProcessfds->cbegin(); it != eventProcessfds->cend(); ++it) {
        if (*it == processfd) {
            eventProcessfds->erase(it);
            break;
        }
    }
}

void FimdImpl::sendExitMessageToMessageQueue(std::shared_ptr<fim::FimdHandle> watcher) {
    // @TODO: document this
    if (mq_send(watcher->mqfd(), MQ_EXIT_MESSAGE, strlen(MQ_EXIT_MESSAGE), 1) == EOF) {
        // do stuff
    }
}
} // namespace fimd
