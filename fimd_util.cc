#include "fimd_util.h"

#include <glob.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fimd {
// @TODO: document this
int FimdUtil::getPidForContainer(std::string id) {
    int pid = 0;
    std::string cgroup_type = "memory";
    std::string cgroup_root = findCgroupMountpoint(cgroup_type);
    std::string cgroup_this = getThisCgroup(cgroup_type);

    id += '*';
    std::vector<std::string> attempts = {
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
        auto files = fglob(*it);
        if (files.size() > 0) {
            std::ifstream output(files[0]);
            std::string line;
            std::getline(output, line);
            pid = atoi(line.c_str());
        }
    }
    return pid;
}

std::vector<std::string> FimdUtil::fglob(const std::string &pattern) {
    std::vector<std::string> filenames;
    glob_t glob_result;
    int err = glob(pattern.c_str(), GLOB_TILDE, NULL, &glob_result);
    if (err == 0) {
        for (size_t i = 0; i < glob_result.gl_pathc; ++i) {
            filenames.push_back(std::string(glob_result.gl_pathv[i]));
        }
    }
    globfree(&glob_result);
    return filenames;
}

std::string FimdUtil::findCgroupMountpoint(std::string cgroup_type) {
    std::ifstream output("/proc/mounts");
    std::string line;
    // /proc/mounts has 6 fields per line, one mount per line, e.g.
    // cgroup /sys/fs/cgroup/devices cgroup rw,relatime,devices 0 0
    while (std::getline(output, line)) {
        std::string fs_spec, fs_file, fs_vfstype, fs_mntops, fs_freq, fs_passno;
        output >> fs_spec >> fs_file >> fs_vfstype >> fs_mntops >> fs_freq >> fs_passno;
        if (fs_vfstype == "cgroup") {
            std::vector<std::string> results = split(fs_mntops, ',');
            for (auto it = results.begin(); it != results.end(); ++it) {
                if ((*it) == cgroup_type) {
                    return fs_file;
                }
            }
        }
    }
}

// returns the relative path to the cgroup docker is running in
std::string FimdUtil::getThisCgroup(std::string cgroup_type) {
    std::ifstream dockerpid("/var/run/docker.pid");
    std::string line;
    std::getline(dockerpid, line);
    // split by \n, check if len == 0 || len result[0] == 0
    int pid = atoi(line.c_str());

    std::stringstream ss;
    ss << "/proc/" << pid << "/cgroup";
    std::ifstream output(ss.str());
    while (std::getline(output, line)) {
        auto results = split(line, ':');
        if (results[1] == cgroup_type) {
            return results[2];
        }
    }
}
} // namespace fimd
