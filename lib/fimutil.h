#ifndef __FIM_UTIL__
#define __FIM_UTIL__

#include <stdbool.h>
#include <stdint.h>

#ifndef DEBUG
#define DEBUG 1
#endif

// @TODO: change this to dynamic allocation?
#define WATCH_MAX (1<<15)

struct fimwatch {
    int pid, sid;          // pid, subject id
    int slot;              // wlcache slot
    int fd;                // inotify file descriptor
    int wd[WATCH_MAX];     // array of watch descriptors (-1 if slot unused)
    int rootpathc;         // cached path count
    char **rootpaths;      // cached path name(s)
    struct stat *rootstat; // `stat` structures for root directories
    int ignored_rootpathc; //
    int pathc;             // cached path count, including recursive traversal
    char **paths;          // cached path name(s), including recursive traversal
    uint32_t event_mask;
    bool only_dir;
    bool recursive;
};

struct fimwatch *wlcache; // array of cached watches
int wlcachec;

void join_namespace(const pid_t pid, const char *ns);

#endif
