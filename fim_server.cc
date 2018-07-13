#include <fstream>
#include <sstream>
#include <string>
#include <sys/inotify.h>
#include <vector>

#include <grpc/grpc.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/security/server_credentials.h>

#include "fim_server.h"
extern "C" {
#include "fimnotify/fimnotify.h"
}

using namespace std;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using fim::Fimd;
using fim::FimdConfig;
using fim::FimdHandle;
using fim::FimWatcherSubject;

#define PORT 50051

string findCgroupMountpoint(string cgroup_type) {
    ifstream output("/proc/mounts");
    string line;
    // /proc/mounts has 6 fields per line, one mount per line, e.g.
    // cgroup /sys/fs/cgroup/devices cgroup rw,relatime,devices 0 0
    while (getline(output, line)) {
        string fs_spec, fs_file, fs_vfstype, fs_mntops, fs_freq, fs_passno;
        output >> fs_spec >> fs_file >> fs_vfstype >> fs_mntops >> fs_freq >> fs_passno;
        if (fs_vfstype == "cgroup") {
            vector<string> results = split(fs_mntops, ',');
            for (auto it = results.begin(); it != results.end(); ++it) {
                if ((*it) == cgroup_type) {
                    return fs_file;
                }
            }
        }
    }
}

// returns the relative path to the cgroup docker is running in
string getThisCgroup(string cgroup_type) {
    ifstream dockerpid("/var/run/docker.pid");
    string line;
    getline(dockerpid, line);
    // split by \n, check if len == 0 || len result[0] == 0
    int pid = atoi(line.c_str());

    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "/proc/%d/cgroup", pid);
    ifstream output(buffer);
    while (getline(output, line)) {
        vector<string> results = split(line, ':');
        if (results[1] == cgroup_type) {
            return results[2];
        }
    }
}

int getPidForContainer(string id) {
    int pid = 0;
    string cgroup_type = "memory";
    string cgroup_root = findCgroupMountpoint(cgroup_type);
    string cgroup_this = getThisCgroup(cgroup_type);

    id += '*';
    vector<string> attempts = {
        cgroup_root + cgroup_this + '/' + id + "/tasks",
        // with more recent lxc, cgroup will be in lxc/
        cgroup_root + cgroup_this + "/lxc/" + id + "/tasks",
        // with more recent docker, cgroup will be in docker/
        cgroup_root + cgroup_this + "/docker/" + id + "/tasks",
        // even more recent docker versions under systemd, use docker-<id>.scope/
        cgroup_root + "/system.slice/docker-" + id + ".scope/tasks",
        // even more recent docker versions under cgroup/systemd/docker/<id>/
        cgroup_root + "/../systemd/docker/" + id + "/tasks",
        // kubernetes with docker and CNI is even more different
        cgroup_root + "/../systemd/kubepods/*/pod*/" + id + "/tasks"
    };

    for (auto it = attempts.begin(); it != attempts.end(); ++it) {
        vector<string> files = glob(*it);
        if (files.size() > 0) {
            ifstream output(files[0]);
            string line;
            getline(output, line);
            pid = atoi(line.c_str());
        }
    }
    return pid;
}

Status FimdImpl::NewWatch(ServerContext *context, const FimdConfig *request, FimdHandle *response) {
    const string &containerid = erase_substr(request->containerid(), "docker://");
    int pid = getPidForContainer(containerid);
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

int main(int argc, char **argv) {
    stringstream ss;
    ss << "0.0.0.0:" << PORT;
    string server_address(ss.str());
    FimdImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    unique_ptr<grpc::Server> server(builder.BuildAndStart());
    cout << "Server listening on " << server_address << endl;
    server->Wait();
    return 0;
}
