#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>
#include <string>
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

Status FimdImpl::NewWatch(ServerContext *context, const FimdConfig *request, FimdHandle *response) {
    const string &containerid = FimdUtil::eraseSubstr(request->containerid(), "docker://");
    int pid = FimdUtil::getPidForContainer(containerid);
    cout << "NewWatch: [" << containerid << " | pid: " << pid << "]" << endl;
    if (pid == 0) {
        return Status::CANCELLED;
    }

    int i, j;
    for (i = 0; i < request->subjects_size(); ++i) {
        const FimWatcherSubject &subject = request->subjects(i);
        // @TODO: wait until watch dir is available? (retry queue?)
        vector<string> pathvec;
        for (j = 0; j < subject.paths_size(); ++j) {
            stringstream ss;
            ss << "/proc/" << pid << "/root" << subject.paths(j).c_str();
            pathvec.push_back(ss.str());
        }
		char **patharr = new char *[pathvec.size()];
		for(size_t x = 0; x < pathvec.size(); x++){
			patharr[x] = new char[pathvec[x].size() + 1];
			strcpy(patharr[x], pathvec[x].c_str());
		}

        uint32_t event_mask = 0;
        for (j = 0; j < subject.events_size(); ++j) {
            const char *event = subject.events(j).c_str();
            if (strcmp(event, "all") == 0)         event_mask |= IN_ALL_EVENTS;
            else if (strcmp(event, "access") == 0) event_mask |= IN_ACCESS;
            else if (strcmp(event, "modify") == 0) event_mask |= IN_MODIFY;
            else if (strcmp(event, "attrib") == 0) event_mask |= IN_ATTRIB;
            else if (strcmp(event, "open") == 0)   event_mask |= IN_OPEN;
            else if (strcmp(event, "close") == 0)  event_mask |= IN_CLOSE;
            else if (strcmp(event, "create") == 0) event_mask |= IN_CREATE;
            else if (strcmp(event, "delete") == 0) event_mask |= IN_DELETE;
            else if (strcmp(event, "move") == 0)   event_mask |= IN_MOVE;
            // @TODO: handle mask_add (IN_MASK_ADD) for existing watcher
        }

        cout << "[server] Starting inotify watcher..." << endl;

		packaged_task<void(int, char **, uint32_t)> task(start_inotify_watcher);
		thread task_td(move(task), subject.paths_size(), (char **)patharr, (uint32_t)event_mask);
		task_td.detach();
        m_watchers.push_back(&task_td);
    }

    return Status::OK;
}
