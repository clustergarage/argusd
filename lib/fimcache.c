#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>

#include "fimcache.h"
#include "fimutil.h"

extern struct fimwatch *wlcache[WATCH_MAX];
extern int wlcachec[WATCH_MAX];

/**
 * deallocate the watch cache
 */
void free_cache(const int pid) {
    free(wlcache[pid]);
    wlcachec[pid] = 0;
    wlcache[pid] = NULL;
}

/**
 * check that all path names in the cache are valid and refer to directories
 */
void check_cache_consistency(const int pid) {
    struct stat sb;
    int i, j;

    for (i = 0; i < wlcachec[pid]; ++i) {
        if (wlcache[pid][i].pathc == -1) {
            continue;
        }

        for (j = 0; j < wlcache[pid][i].pathc; ++j) {
            if (lstat(wlcache[pid][i].paths[j], &sb) == EOF) {
#if DEBUG
                printf("check_cache_consistency: stat: [slot = %d; wd = %d] %s: %s\n",
                    i, wlcache[pid][i].wd[j], wlcache[pid][i].paths[j], strerror(errno));
                fflush(stdout);
#endif
                remove_item_from_cache(&wlcache[pid][i], j);
                continue;
            }

#if ONLY_DIR
            if (!S_ISDIR(sb.st_mode)) {
#if DEBUG
                fprintf(stderr, "check_cache_consistency: %s is not a directory\n", wlcache[pid][i].paths[j]);
#endif
                continue;
            }
#endif
        }
    }
}

void remove_item_from_cache(struct fimwatch *watch, int const index) {
    int i;
    for (i = index; i < watch->pathc - 1; ++i) {
        watch->wd[i] = watch->wd[i + 1];
        watch->paths[i] = watch->paths[i + 1];
    }
    --watch->pathc;
}

/**
 * check whether the cache contains the watch descriptor `wd`
 * if found, return the slot number, otherwise return -1
 */
int find_watch(const int pid, const int wd) {
    int i, j;
    for (i = 0; i < wlcachec[pid]; ++i) {
        for (j = 0; j < wlcache[pid][i].pathc; ++j) {
            if (wlcache[pid][i].wd[j] == wd) {
                return i;
            }
        }
    }
    return -1;
}

/**
 * deallocate the watch cache
 */
int find_watch_checked(const int pid, const int wd) {
    int slot = find_watch(pid, wd);
    if (slot > -1) {
        return slot;
    }

#if DEBUG
    printf("could not find watch: %d\n", wd);
    fflush(stdout);
#endif
    // returning -1 to our caller identifies that there's a problem, and the
    // caller should probably trigger a cache rebuild
    return -1;
}

/**
 * mark a cache entry as unused
 */
void mark_cache_slot_empty(const int pid, const int slot) {
    int i;
    /*
#if DEBUG
    printf("        mark_cache_slot_empty: pid = %d; slot = %d\n", pid, slot);
    fflush(stdout);
#endif
    */
    for (i = 0; i < wlcache[pid][slot].pathc; ++i) {
        wlcache[pid][slot].wd[i] = -1;
        wlcache[pid][slot].paths[i] = '\0';
    }
    wlcache[pid][slot].pathc = -1;
    wlcache[pid][slot].event_mask = -1;
    wlcache[pid][slot].recursive = false;
}

/**
 * find a free slot in the cache
 */
static int find_empty_cache_slot(const int pid) {
    const int ALLOC_INC = 128;
    int i, j;

    for (i = 0; i < wlcachec[pid]; ++i) {
        if (wlcache[pid][i].pathc == -1) {
            return i;
        }
    }
    // no free slot found; resize cache
    wlcachec[pid] += ALLOC_INC;

    wlcache[pid] = realloc(wlcache[pid], wlcachec[pid] * sizeof(struct fimwatch));
#if DEBUG
    if (wlcache[pid] == NULL) {
        perror("realloc");
    }
#endif

    for (i = wlcachec[pid] - ALLOC_INC; i < wlcachec[pid]; ++i) {
        mark_cache_slot_empty(pid, i);
    }

    // return first slot in newly allocated space
    return wlcachec[pid] - ALLOC_INC;
}

/**
 * add an item to the cache
 */
void add_watch_to_cache(const int pid, const struct fimwatch *watch) {
    int slot = find_empty_cache_slot(pid);
    wlcache[pid][slot] = *watch;
}

/**
 * return the cache slot that corresponds to a particular path name
 * or -1 if the path is not in the cache
 */
int path_name_to_cache_slot(const int pid, const char *path) {
    int i, j;
    for (i = 0; i < wlcachec[pid]; ++i) {
        if (wlcache[pid][i].pathc > -1) {
            for (j = 0; j < wlcache[pid][i].pathc; ++j) {
                if (strcmp(wlcache[pid][i].paths[j], path) == 0) {
                    return i;
                }
            }
        }
    }
    return -1;
}

char *wd_to_path_name(const int pid, const int wd) {
    int i, j;
    for (i = 0; i < wlcachec[pid]; ++i) {
        for (j = 0; j < wlcache[pid][i].pathc; ++j) {
            if (wlcache[pid][i].wd[j] == wd) {
                return wlcache[pid][i].paths[j];
            }
        }
    }
}

int wd_to_cache_slot(const int pid, const int wd) {
    int i, j;
    for (i = 0; i < wlcachec[pid]; ++i) {
        for (j = 0; j < wlcache[pid][i].pathc; ++j) {
            if (wlcache[pid][i].wd[j] == wd) {
                return j;
            }
        }
    }
    return -1;
}
