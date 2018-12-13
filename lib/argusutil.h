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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#ifndef DEBUG
#define DEBUG 1
#endif

#define IN_EVENT_LEN (sizeof(struct inotify_event))
#define IN_BUFFER_SIZE (IN_EVENT_LEN + NAME_MAX + 1)
#define IN_EVENT_NEXT(evt, len, evtlen) ((struct inotify_event *)(((char *)(evt)) + (evtlen)))
#define IN_EVENT_OK(evt, buf, len) ((char *)(evt) < (char *)(buf) + (len))

#define FORMAT_PATH(fp, dir, file) do {           \
    snprintf(fp, sizeof(fp), "%s/%s", dir, file); \
} while(0)

#define DUMP_CACHE(watch) do {                                               \
    printf("  $$$$ watch = %p:\n", (void *)(watch));                          \
    printf("    $$   fd = %d\n", (watch)->fd);                               \
    printf("    $$   rootpathc = %d\n", (watch)->rootpathc);                 \
    fflush(stdout);                                                          \
    for (int i = 0; i < (watch)->rootpathc; ++i) {                           \
        printf("     $     rootpaths[%d] = %s\n", i, (watch)->rootpaths[i]); \
        fflush(stdout);                                                      \
    }                                                                        \
    printf("    $$   pathc = %d\n", (watch)->pathc);                         \
    for (int i = 0; i < (watch)->pathc; ++i) {                               \
        printf("     $     wd[%d] = %d\n", i, (watch)->wd[i]);               \
        printf("     $     paths[%d] = %s\n", i, (watch)->paths[i]);         \
        fflush(stdout);                                                      \
    }                                                                        \
    printf("    $$   event_mask = %d\n", (watch)->event_mask);               \
    printf("    $$   recursive = %d\n", (watch)->recursive);                 \
    fflush(stdout);                                                          \
} while(0)

struct arguswatch {
    const char *name;                 // Name of ArgusWatcher.
    int pid, sid;                     // PID, Subject ID.
    const char *node_name, *pod_name; // Name of node, pod in which process is running.
    int slot;                         // `wlcache` slot.
    int fd;                           // `inotify` file descriptor.
    int *wd;                          // Array of watch descriptors (-1 if slot unused).
    unsigned int rootpathc;           // Cached path count.
    char **rootpaths;                 // Cached path name(s).
    struct stat *rootstat;            // `stat` structures for root directories.
    unsigned int pathc;               // Cached path count, including recursive traversal.
    char **paths;                     // Cached path name(s), including recursive traversal.
    unsigned int ignorec;             // Ignore path pattern count.
    char **ignores;                   // Ignore path patterns.
    unsigned int ignored_rootpathc;   // Ignored rootpath count.
    uint32_t event_mask;              // Event mask for `inotify`.
    bool only_dir;                    // Flag to watch only directories.
    bool recursive;                   // Flag to watch recursively.
    int max_depth;                    // Max `nftw` depth to recurse through.
    int processevtfd;                 // Anonymous pipe to send watch kill signal.
    const char *tags;                 // Custom tags for printing ArgusWatcher event.
    const char *log_format;           // Custom logging format for printing ArgusWatcher event.
};

struct arguswatch **wlcache; // Array of cached watches.
int wlcachec;

#endif
