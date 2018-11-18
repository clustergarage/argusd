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

#ifndef __ARGUS_UTIL__
#define __ARGUS_UTIL__

#include <mqueue.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#ifndef DEBUG
#define DEBUG 0
#endif

struct arguswatch {
    int pid, sid;                   // PID, Subject ID.
    int slot;                       // `wlcache` slot.
    int fd;                         // `inotify` file descriptor.
    int *wd;                        // Array of watch descriptors (-1 if slot unused).
    unsigned int rootpathc;         // Cached path count.
    char **rootpaths;               // Cached path name(s).
    struct stat *rootstat;          // `stat` structures for root directories.
    unsigned int pathc;             // Cached path count, including recursive traversal.
    char **paths;                   // Cached path name(s), including recursive traversal.
    unsigned int ignorec;           // Ignore path pattern count.
    char **ignores;                 // Ignore path patterns.
    unsigned int ignored_rootpathc; // Ignored rootpath count.
    uint32_t event_mask;            // Event mask for `inotify`.
    bool only_dir;                  // Flag to watch only directories.
    bool recursive;                 // Flag to watch recursively.
    int max_depth;                  // Max `nftw` depth to recurse through.
    int processevtfd;               // Anonymous pipe to send watch kill signal.
    mqd_t mq;                       // Message queue file descriptor.
};

struct arguswatch *wlcache; // Array of cached watches.
int wlcachec;

void join_namespace(pid_t pid, const char *ns);

#endif
