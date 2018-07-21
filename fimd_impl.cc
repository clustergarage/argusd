#include <memory>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <thread>
#include <vector>
#include <mqueue.h>

#include <glog/logging.h>
#include <grpc/grpc.h>
#include <grpc++/server_context.h>

#include "fimd_impl.h"
#include "fimd_util.h"
extern "C" {
#include "lib/fimnotify.h"
}

using namespace std;
using fim::Fimd;
using fim::FimdConfig;
using fim::FimdHandle;
using fim::FimWatcherSubject;
using fim::Empty;
using google::protobuf::RepeatedField;
using grpc::ServerContext;
using grpc::Status;

Status FimdImpl::CreateWatch(ServerContext *context, const FimdConfig *request, FimdHandle *response) {
    vector<int> pids = getPidsFromRequest(request);
    if (!pids.size()) {
        return Status::CANCELLED;
    }

    // find existing watcher by pid in case we need to update
    // inotify_add_watcher is designed to both add and modify depending
    // on if a fd exists already for this path
    shared_ptr<FimdHandle> watcher = findFimdWatcherByPids(request->hostuid(), pids);

    if (watcher == nullptr) {
        LOG(INFO) << "Starting inotify watcher...";
    } else {
        LOG(INFO) << "Updating inotify watcher...";

        // stop existing watcher polling
        sendKillSignalToWatcher(watcher);
        watcher->clear_processeventfd();
    }

    response->set_hostuid(request->hostuid().c_str());
    for_each(pids.cbegin(), pids.cend(), [&](const int pid) {
        for_each(request->subject().cbegin(), request->subject().cend(), [&](const FimWatcherSubject subject) {
            // @TODO: check if any watchers are started, if not, don't add to response
            createInotifyWatcher(subject, getPathArrayFromSubject(pid, subject),
                getEventMaskFromSubject(subject), response->mutable_processeventfd());
        });
        response->add_pid(pid);
    });

    if (watcher == nullptr) {
        // store new watcher
        m_watchers.push_back(make_shared<FimdHandle>(*response));
    } else {
        for_each(response->processeventfd().cbegin(), response->processeventfd().cend(), [&](const int processfd) {
            watcher->add_processeventfd(processfd);
        });
    }

    // start message queue
    packaged_task<void()> queue(startMessageQueue);
    thread queueThread(move(queue));
    // start as daemon process
    queueThread.detach();

    return Status::OK;
}

Status FimdImpl::DestroyWatch(ServerContext *context, const FimdConfig *request, Empty *response) {
    vector<int> pids = getPidsFromRequest(request);
    if (!pids.size()) {
        return Status::CANCELLED;
    }

    LOG(INFO) << "Stopping inotify watcher...";

    shared_ptr<FimdHandle> watcher = findFimdWatcherByPids(request->hostuid(), pids);
    if (watcher != nullptr) {
        sendKillSignalToWatcher(watcher);
    }
    m_watchers.erase(remove(m_watchers.begin(), m_watchers.end(), watcher), m_watchers.end());

    return Status::OK;
}

vector<int> FimdImpl::getPidsFromRequest(const FimdConfig *request) {
    vector<int> pids;
    for_each(request->containerid().cbegin(), request->containerid().cend(), [&](const string containerId) {
        int pid = FimdUtil::getPidForContainer(cleanContainerId(containerId));
        if (pid) {
            pids.push_back(pid);
        }
    });
    return pids;
}

shared_ptr<FimdHandle> FimdImpl::findFimdWatcherByPids(const std::string hostUid, const vector<int> pids) {
    auto it = find_if(m_watchers.cbegin(), m_watchers.cend(), [&](shared_ptr<FimdHandle> watcher) {
        bool foundPid;
        for (auto pid = pids.cbegin(); pid != pids.cend(); ++pid) {
            auto watcherPid = find_if(watcher->pid().cbegin(), watcher->pid().cend(),
                [&](int p) { return p == *pid; });
            foundPid = watcherPid != watcher->pid().cend();
        }
        return watcher->hostuid() == hostUid && foundPid;
    });
    if (it != m_watchers.cend()) {
        return *it;
    }
    return nullptr;
}

char **FimdImpl::getPathArrayFromSubject(const int pid, const FimWatcherSubject subject) {
    vector<string> pathvec;
    for_each(subject.path().cbegin(), subject.path().cend(), [&](string path) {
        stringstream ss;
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

uint32_t FimdImpl::getEventMaskFromSubject(const FimWatcherSubject subject) {
    // @TODO: document this - also: IN_EXCL_UNLINK
    uint32_t mask = 0; //IN_DONT_FOLLOW;
    for_each(subject.event().cbegin(), subject.event().cend(), [&](string event) {
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

void FimdImpl::createInotifyWatcher(const FimWatcherSubject subject, char **patharr, uint32_t event_mask,
    RepeatedField<google::protobuf::int32> *eventProcessfds) {
    // create anonymous pipe to communicate with inotify watcher
    int processfd = eventfd(0, EFD_CLOEXEC);
    if (processfd == EOF) {
        return;
    }
    eventProcessfds->Add(processfd);

    packaged_task<int(int, char **, uint32_t, int)> task(start_inotify_watcher);
    future<int> result = task.get_future();

    thread taskThread(move(task), subject.path_size(), (char **)patharr, (uint32_t)event_mask, processfd);
    // start as daemon process
    taskThread.detach();

    // @TODO: re-evaluate this timeout
    future_status status = result.wait_for(100ms);
    if (status == future_status::ready &&
        result.get() != EXIT_SUCCESS) {
        eraseEventProcessfd(eventProcessfds, processfd);
    }
}

void FimdImpl::startMessageQueue() {
    mqd_t mq;
    mq_attr attr;
    fimwatch_event *fwevent;
    char buffer[MQ_MAX_SIZE + 1];
    ssize_t bytes_read;
    bool done;

    // initialize the queue attributes
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MQ_MAX_SIZE;
    attr.mq_curmsgs = 0;

    // create the message queue
    mq = mq_open(MQ_QUEUE_NAME, O_CREAT | O_RDONLY, 0644, &attr);

    do {
        bytes_read = mq_receive(mq, buffer, MQ_MAX_SIZE, NULL);
        buffer[bytes_read] = '\0';
        if (!strncmp(buffer, MQ_EXIT_MESSAGE, strlen(MQ_EXIT_MESSAGE))) {
            done = true;
        } else {
            fwevent = (struct fimwatch_event *)buffer;
            stringstream ss;

            if (fwevent->event_mask & IN_ACCESS) ss << "IN_ACCESS";
            else if (fwevent->event_mask & IN_MODIFY) ss << "IN_MODIFY";
            else if (fwevent->event_mask & IN_ATTRIB) ss << "IN_ATTRIB";
            else if (fwevent->event_mask & IN_OPEN) ss << "IN_OPEN";
            else if (fwevent->event_mask & IN_CLOSE_WRITE) ss << "IN_CLOSE_WRITE";
            else if (fwevent->event_mask & IN_CLOSE_NOWRITE) ss << "IN_CLOSE_NOWRITE";
            else if (fwevent->event_mask & IN_CREATE) ss << "IN_CREATE";
            else if (fwevent->event_mask & IN_DELETE) ss << "IN_DELETE";
            else if (fwevent->event_mask & IN_DELETE_SELF) ss << "IN_DELETE_SELF";
            else if (fwevent->event_mask & IN_MOVED_FROM) ss << "IN_MOVED_FROM";
            else if (fwevent->event_mask & IN_MOVED_TO) ss << "IN_MOVED_TO";
            else if (fwevent->event_mask & IN_MOVE_SELF) ss << "IN_MOVE_SELF";
            // IN_IGNORED called when oneshot is active
            //else break;

            // @TODO: replace /proc/$PID/root for log-readability
            ss << ": " << fwevent->path_name << "/" << fwevent->file_name <<
                " [" << (fwevent->is_dir & IN_ISDIR ? "directory" : "file") << "]";
            LOG(INFO) << ss.str();
        }
    } while (!done);

    mq_close(mq);
    mq_unlink(MQ_QUEUE_NAME);
}

void FimdImpl::sendKillSignalToWatcher(shared_ptr<FimdHandle> watcher) {
    // kill existing watcher polls
    uint64_t value = FIMNOTIFY_KILL;
    for_each(watcher->processeventfd().cbegin(), watcher->processeventfd().cend(), [&](const int processfd) {
        write(processfd, &value, sizeof(uint64_t));
        eraseEventProcessfd(watcher->mutable_processeventfd(), processfd);
    });
}

void FimdImpl::eraseEventProcessfd(RepeatedField<google::protobuf::int32> *eventProcessfds, const int processfd) {
    for (auto it = eventProcessfds->cbegin(); it != eventProcessfds->cend(); ++it) {
        if (*it == processfd) {
            eventProcessfds->erase(it);
            break;
        }
    }
}
