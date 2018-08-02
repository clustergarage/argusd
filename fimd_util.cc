#include "fimd_util.h"

#include <fstream>
#include <glob.h>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

// @TODO: document this
int FimdUtil::getPidForContainer(string id) {
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
        vector<string> files = fglob(*it);
        if (files.size() > 0) {
            ifstream output(files[0]);
            string line;
            getline(output, line);
            pid = atoi(line.c_str());
        }
    }
    return pid;
}

vector<string> FimdUtil::fglob(const string &pattern) {
    vector<string> filenames;
    glob_t glob_result;
    int err = glob(pattern.c_str(), GLOB_TILDE, NULL, &glob_result);
    if (err == 0) {
        for (size_t i = 0; i < glob_result.gl_pathc; ++i) {
            filenames.push_back(string(glob_result.gl_pathv[i]));
        }
    }
    globfree(&glob_result);
    return filenames;
}

string FimdUtil::findCgroupMountpoint(string cgroup_type) {
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
string FimdUtil::getThisCgroup(string cgroup_type) {
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
