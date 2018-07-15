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
using grpc::ServerContext;
using grpc::Status;

Status FimdImpl::CreateWatch(ServerContext *context, const FimdConfig *request, FimdHandle *response) {
    const string &containerId = FimdUtil::eraseSubstr(request->containerid(), "docker://");
    int pid = FimdUtil::getPidForContainer(containerId);
    if (!pid) {
        return Status::CANCELLED;
    }

	// find existing watcher by pid in case we need to update
	// inotify_add_watcher is designed to both add and modify depending
	// on if a fd exists already for this path
    shared_ptr<FimdWatcher> watcher = findFimdWatcherByPid(pid);

    vector<int> procFds;

    for (int i = 0; i < request->subjects_size(); ++i) {
        const FimWatcherSubject &subject = request->subjects(i);

		cout << "[server] " << (watcher == nullptr ? "Starting" : "Updating") << " inotify watcher..." << endl;

		if (watcher != nullptr) {
			// stop existing watcher polling
			sendKillSignalToWatcher(watcher);
		}

		createInotifyWatcher(subject, getPathArrayFromSubject(pid, subject),
			getEventMaskFromSubject(subject), &procFds);
    }

    if (watcher == nullptr) {
		// create and store new watcher
        FimdWatcher fw = {pid, procFds};
        m_watchers.push_back(make_shared<FimdWatcher>(fw));
    } else {
		// update proc event fds on existing watcher
		watcher->processEventfds = procFds;
    }

    return Status::OK;
}

Status FimdImpl::DestroyWatch(ServerContext *context, const FimdConfig *request, FimdHandle *response) {
    const string &containerId = FimdUtil::eraseSubstr(request->containerid(), "docker://");
    int pid = FimdUtil::getPidForContainer(containerId);
    if (!pid) {
        return Status::CANCELLED;
    }

    shared_ptr<FimdWatcher> watcher = findFimdWatcherByPid(pid);
    if (watcher != nullptr) {
		sendKillSignalToWatcher(watcher);
    }

    return Status::OK;
}

shared_ptr<FimdImpl::FimdWatcher> FimdImpl::findFimdWatcherByPid(const int pid) {
    auto it = find_if(m_watchers.cbegin(), m_watchers.cend(),
        [&](shared_ptr<FimdWatcher> watcher) { return watcher->pid == pid; });
    if (it != m_watchers.cend()) {
        return *it;
    }
    return nullptr;
}

char **FimdImpl::getPathArrayFromSubject(const int pid, const FimWatcherSubject subject) {
	vector<string> pathvec;
	for (int j = 0; j < subject.paths_size(); ++j) {
		stringstream ss;
		ss << "/proc/" << pid << "/root" << subject.paths(j).c_str();
		pathvec.push_back(ss.str());
	}
	char **patharr = new char *[pathvec.size()];
	for(size_t x = 0; x < pathvec.size(); x++){
		patharr[x] = new char[pathvec[x].size() + 1];
		strcpy(patharr[x], pathvec[x].c_str());
	}
	return patharr;
}

uint32_t FimdImpl::getEventMaskFromSubject(const FimWatcherSubject subject) {
	uint32_t mask = 0;
	// @TODO: IN_EXCL | IN_DONT_FOLLOW ...
	// for cases when more than one path is being watched; A,B - where B links to A, etc.
	for (int j = 0; j < subject.events_size(); ++j) {
		const char *event = subject.events(j).c_str();
		if (strcmp(event, "all") == 0)         mask |= IN_ALL_EVENTS;
		else if (strcmp(event, "access") == 0) mask |= IN_ACCESS;
		else if (strcmp(event, "modify") == 0) mask |= IN_MODIFY;
		else if (strcmp(event, "attrib") == 0) mask |= IN_ATTRIB;
		else if (strcmp(event, "open") == 0)   mask |= IN_OPEN;
		else if (strcmp(event, "close") == 0)  mask |= IN_CLOSE;
		else if (strcmp(event, "create") == 0) mask |= IN_CREATE;
		else if (strcmp(event, "delete") == 0) mask |= IN_DELETE;
		else if (strcmp(event, "move") == 0)   mask |= IN_MOVE;
	}
	return mask;
}

void FimdImpl::createInotifyWatcher(const FimWatcherSubject subject, char **patharr, uint32_t event_mask, vector<int> *procFds) {
	// create anonymous pipe to communicate with inotify watcher
	int procFd = eventfd(0, EFD_CLOEXEC);
	if (procFd == EOF) {
		return;
	}
	procFds->push_back(procFd);

	packaged_task<void(int, char **, uint32_t, int)> task(start_inotify_watcher);
	thread taskThread(move(task), subject.paths_size(), (char **)patharr, (uint32_t)event_mask, procFd);
	taskThread.detach();
}

void FimdImpl::sendKillSignalToWatcher(shared_ptr<FimdWatcher> watcher) {
	// kill existing watcher polls
	uint64_t value = FIMNOTIFY_KILL;
	for_each(watcher->processEventfds.cbegin(), watcher->processEventfds.cend(), [&](int procFd) {
		write(procFd, &value, sizeof(value));
	});
}
