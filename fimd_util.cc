/**
 * MIT License
 *
 * Copyright (c) 2018 ClusterGarage
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "fimd_util.h"

#include <glob.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fimd {
/**
 * find the container runtime given a string prefixed with a protocol
 * currently only supports docker,cri-o,rkt,containerd
 */
std::string FimdUtil::findContainerRuntime(const std::string containerId) {
    std::vector<std::string> runtimes{"docker", "cri-o", "rkt", "containerd"};
    for (auto it = runtimes.cbegin(); it != runtimes.cend(); ++it) {
        if (containerId.compare(0, (*it).length(), (*it)) == 0) {
            return (*it);
        }
    }
    // Default to docker for now.
    return "docker";
}

/**
 * Find the process ID given a container ID and runtime. Do this through
 * various lookup attempts on a cgroup.
 * Changelog:
 *  - modified to glob with id
 *  - modified to search for newer docker containers
 *  - modified to search for newer kubernetes+docker versions
 *  - modified to search cri-o, rkt, containerd sources
 */
int FimdUtil::getPidForContainer(std::string id, const std::string runtime) {
    int pid = 0;
    std::vector<std::string> attempts;

    id += '*';
    if (runtime == "docker") {
        std::vector<std::string> cgroups{"memory", "cpu", "cpuacct", "cpuset"};
        for (auto it = cgroups.cbegin(); it != cgroups.cend(); ++it) {
            // Memory cgroup is chosen randomly. Any cgroup used by docker
            // works.
            std::string cgroupRoot = findCgroupMountpoint((*it));
            std::string cgroupThis = getThisCgroup((*it), runtime);

            attempts.push_back(cgroupRoot + cgroupThis + '/' + id + "/tasks");
            // With more recent lxc, cgroup will be in lxc/.
            attempts.push_back(cgroupRoot + cgroupThis + "/lxc/" + id + "/tasks");
            // With more recent docker, cgroup will be in docker/.
            attempts.push_back(cgroupRoot + cgroupThis + "/docker/" + id + "/tasks");
            // Even more recent docker versions under systemd, use
            // docker-<id>.scope/.
            attempts.push_back(cgroupRoot + "/system.slice/docker-" + id + ".scope/tasks");
            // Even more recent docker versions under
            // cgroup/systemd/docker/<id>/
            attempts.push_back(cgroupRoot + "/../systemd/docker/" + id + "/tasks");
            // Kubernetes with docker and CNI is even more different.
            attempts.push_back(cgroupRoot + "/../systemd/kubepods/*/pod*/" + id + "/tasks");
            // Another flavor of containers location in recent Kubernetes 1.11+.
            attempts.push_back(cgroupRoot + cgroupThis + "/kubepods.slice/kubepods-besteffort.slice/*/docker-" + id + ".scope/tasks");
            // When running inside of a container with recent Kubernetes 1.11+.
            attempts.push_back(cgroupRoot + "/kubepods.slice/kubepods-besteffort.slice/*/docker-" + id + ".scope/tasks");
        }
    } else if (runtime == "cri-o") {
        attempts.push_back("/var/run/crio/" + id + "/pidfile");
    } else if (runtime == "rkt") {
        attempts.push_back("/var/lib/rkt/pods/run/" + id + "/pid");
    } else if (runtime == "containerd") {
        attempts.push_back("/var/run/containerd/*/*/" + id + "/init.pid");
    }

    for (auto at = attempts.begin(); at != attempts.end(); ++at) {
        auto files = fglob(*at);
        if (files.size() > 0) {
            std::ifstream output(files[0]);
            std::string line;
            std::getline(output, line);
            pid = atoi(line.c_str());
            break;
        }
    }
    return pid;
}

/**
 * Perform a file glob check. Takes `pattern` string and returns result of
 * matches on this glob.
 */
std::vector<std::string> FimdUtil::fglob(const std::string &pattern) {
    std::vector<std::string> filenames;
    glob_t globResult;
    int err = glob(pattern.c_str(), GLOB_TILDE, NULL, &globResult);
    if (err == 0) {
        for (size_t i = 0; i < globResult.gl_pathc; ++i) {
            filenames.push_back(std::string(globResult.gl_pathv[i]));
        }
    }
    globfree(&globResult);
    return filenames;
}

/**
 * Returns the path to the cgroup mountpoint.
 */
std::string FimdUtil::findCgroupMountpoint(const std::string cgroupType) {
    std::ifstream output("/proc/mounts");
    std::string line;
    // /proc/mounts has 6 fields per line, one mount per line, e.g.:
    // cgroup /sys/fs/cgroup/devices cgroup rw,relatime,devices 0 0
    while (std::getline(output, line)) {
        std::string fsSpec, fsFile, fsVfstype, fsMntops, fsFreq, fsPassno;
        output >> fsSpec >> fsFile >> fsVfstype >> fsMntops >> fsFreq >> fsPassno;
        if (fsVfstype == "cgroup") {
            std::vector<std::string> results = split(fsMntops, ',');
            for (auto it = results.begin(); it != results.end(); ++it) {
                if ((*it) == cgroupType) {
                    return fsFile;
                }
            }
        }
    }
    return "";
}

/**
 * Returns the relative path to the cgroup docker is running in.
 */
std::string FimdUtil::getThisCgroup(const std::string cgroupType, const std::string runtime) {
    std::ifstream dockerpid("/var/run/docker.pid");
    std::string line;
    std::getline(dockerpid, line);
    // Split by \n, check if len == 0 || len result[0] == 0.
    int pid = atoi(line.c_str());

    std::stringstream ss;
    ss << "/proc/" << pid << "/cgroup";
    std::ifstream output(ss.str());
    while (std::getline(output, line)) {
        auto results = split(line, ':');
        if (results[1] == cgroupType) {
            return results[2];
        }
    }
    return "";
}
} // namespace fimd
