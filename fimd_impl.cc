#include <memory>
#include <sstream>
#include <string>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <thread>
#include <vector>

#include <grpc/grpc.h>
#include <grpc++/server_context.h>

#include "fimd_impl.h"
#include "fimd_util.h"
extern "C" {
#include "fimnotify/fimnotify.h"
}

using namespace std;
using fim::Fimd;
using fim::FimdConfig;
using fim::FimdHandle;
using fim::FimWatcherSubject;
using fim::Empty;
using grpc::ServerContext;
using grpc::Status;
using ::google::protobuf::RepeatedField;
using ::google::protobuf::int32;

Status FimdImpl::CreateWatch(ServerContext *context, const FimdConfig *request, FimdHandle *response) {
	// @TODO: multiple containerids passed
    int pid = FimdUtil::getPidForContainer(cleanContainerId(request->containerid()));
    if (!pid) {
        return Status::CANCELLED;
    }

	// find existing watcher by pid in case we need to update
	// inotify_add_watcher is designed to both add and modify depending
	// on if a fd exists already for this path
    shared_ptr<FimdHandle> watcher = findFimdWatcherByPid(request->hostuid(), pid);

	RepeatedField<int32> procFds;
	if (watcher != nullptr) {
		//response = watcher.get();
		procFds = *watcher->mutable_processeventfd();
	}

	for_each(request->subject().cbegin(), request->subject().cend(), [&](const FimWatcherSubject subject) {
		cout << "[server] " << (watcher == nullptr ? "Starting" : "Updating") << " inotify watcher..." << endl;

		if (watcher != nullptr) {
			// stop existing watcher polling
			sendKillSignalToWatcher(watcher);
		}
		createInotifyWatcher(subject, getPathArrayFromSubject(pid, subject),
			getEventMaskFromSubject(subject), &procFds);
    });

	// set response object
	response->set_hostuid(request->hostuid().c_str());
	response->add_pid(pid);
	for_each(procFds.cbegin(), procFds.cend(), [&](int procFd) {
		response->add_processeventfd(procFd);
	});

    if (watcher == nullptr) {
		// store new watcher
		m_watchers.push_back(make_shared<FimdHandle>(*response));
    }

    return Status::OK;
}

Status FimdImpl::DestroyWatch(ServerContext *context, const FimdConfig *request, Empty *response) {
    int pid = FimdUtil::getPidForContainer(cleanContainerId(request->containerid()));
    if (!pid) {
        return Status::CANCELLED;
    }

    shared_ptr<FimdHandle> watcher = findFimdWatcherByPid(request->hostuid(), pid);
    if (watcher != nullptr) {
		sendKillSignalToWatcher(watcher);
    }

    return Status::OK;
}

shared_ptr<FimdHandle> FimdImpl::findFimdWatcherByPid(const string hostUid, const int pid) {
    auto it = find_if(m_watchers.cbegin(), m_watchers.cend(), [&](shared_ptr<FimdHandle> watcher) {
		auto pit = find_if(watcher->pid().cbegin(), watcher->pid().cend(),
			[&](int p) { return p == pid; });
		return watcher->hostuid() == hostUid &&
			pit != watcher->pid().cend();
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
	uint32_t mask = 0;
	// @TODO: IN_EXCL | IN_DONT_FOLLOW ...
	// for cases when more than one path is being watched; A,B - where B links to A, etc.
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

void FimdImpl::createInotifyWatcher(const FimWatcherSubject subject, char **patharr, uint32_t event_mask, RepeatedField<int32> *procFds) {
	// create anonymous pipe to communicate with inotify watcher
	int procFd = eventfd(0, EFD_CLOEXEC);
	if (procFd == EOF) {
		return;
	}
	procFds->Add(procFd);

	packaged_task<void(int, char **, uint32_t, int)> task(start_inotify_watcher);
	thread taskThread(move(task), subject.path_size(), (char **)patharr, (uint32_t)event_mask, procFd);
	taskThread.detach();
}

void FimdImpl::sendKillSignalToWatcher(shared_ptr<FimdHandle> watcher) {
	// kill existing watcher polls
	uint64_t value = FIMNOTIFY_KILL;
	for_each(watcher->processeventfd().cbegin(), watcher->processeventfd().cend(), [&](int processEventfd) {
		write(processEventfd, &value, sizeof(value));
	});
}
