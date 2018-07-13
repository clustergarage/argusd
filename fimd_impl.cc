#include <fstream>
#include <sstream>
#include <string>
#include <sys/inotify.h>

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

        // @TODO: wait until watch dir is available (retry queue?)
        char *paths[subject.paths_size()];
        for (j = 0; j < subject.paths_size(); ++j) {
            snprintf(paths[j], 1024, "/proc/%d/root%s", pid, subject.paths(j).c_str());
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
        // @TODO: put output into a channel of some kind
        // @TODO: hold onto watcher events (vector) for later kill/modify operations
        start_inotify_watcher(subject.paths_size(), paths, event_mask);
    }

    cout << "OK" << endl;
    return Status::OK;
}
