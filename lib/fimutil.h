#ifndef __FIM_UTIL__
#define __FIM_UTIL__

#include <stdbool.h>
#include <stdint.h>

#ifndef DEBUG
#define DEBUG 0
#endif

// @TODO: change this to dynamic allocation?
#define WATCH_MAX (1<<15)

struct fimwatch {
    int fd;              // inotify file descriptor
    int wd[WATCH_MAX];   // array of watch descriptors (-1 if slot unused)
    int pathc;           // cached path count
    char **paths;        // cached path name(s)
    uint32_t event_mask;
    bool recursive;
};

struct fimwatch *wlcache[WATCH_MAX]; // array of cached watches
int wlcachec[WATCH_MAX];             // current size of the cached watchlist array

void join_namespace(const pid_t pid, const char *ns);

#endif
