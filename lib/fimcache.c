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

/**
 * deallocate the watch cache
 */
void free_cache() {
    free(wlcache);
    wlcachec = 0;
    wlcache = NULL;
}

/**
 * check that all path names in the cache are valid and refer to directories
 */
void check_cache_consistency() {
    struct stat sb;
    int i;
#if DEBUG
    int failures = 0;
#endif

    for (i = 0; i < wlcachec; ++i) {
        if (wlcache[i].wd > -1) {
            if (lstat(wlcache[i].path_name, &sb) == EOF) {
#if DEBUG
                printf("check_cache_consistency: stat: [slot = %d; wd = %d] %s: %s\n",
                    i, wlcache[i].wd, wlcache[i].path_name, strerror(errno));
                fflush(stdout);
                ++failures;
#endif
            } else if (!S_ISDIR(sb.st_mode)) {
#if DEBUG
                fprintf(stderr, "check_cache_consistency: %s is not a directory\n",
                    wlcache[i].path_name);
                perror("lstat");
#endif
                return;
            }
        }
    }

#if DEBUG
    if (failures > 0) {
        printf("check_cache_consistency: %d failures\n", failures);
        fflush(stdout);
    }
#endif
}

/**
 * check whether the cache contains the watch descriptor `wd`
 * if found, return the slot number, otherwise return -1
 */
int find_watch(int wd) {
    int i;
    for (i = 0; i < wlcachec; ++i) {
        // @TODO: need to check more than just wd? pid? container id?
        if (wlcache[i].wd == wd) {
            return i;
        }
    }
    return -1;
}

/**
 * deallocate the watch cache
 */
int find_watch_checked(int wd) {
    int slot = find_watch(wd);
    if (wd > -1) {
        return slot;
    }

#if DEBUG
    printf("Could not find watch: %d\n", wd);
    fflush(stdout);
#endif
    // returning -1 to our caller identifies that there's a problem, and the
    // caller should probably trigger a cache rebuild
    return -1;
}

/**
 * mark a cache entry as unused
 */
void mark_cache_slot_empty(int slot) {
#if DEBUG
    printf("        mark_cache_slot_empty: slot = %d;  wd = %d; path = %s\n",
        slot, wlcache[slot].wd, wlcache[slot].path_name);
    fflush(stdout);
#endif
    wlcache[slot].wd = -1;
    wlcache[slot].path_name[0] = '\0';
    wlcache[slot].event_mask = -1;
    wlcache[slot].recursive = false;
}

/**
 * find a free slot in the cache
 */
static int find_empty_cache_slot() {
    int i;
    const int ALLOC_INCR = 200;

    for (i = 0; i < wlcachec; ++i) {
        if (wlcache[i].wd == -1) {
            return i;
        }
    }
    // no free slot found; resize cache
    wlcachec += ALLOC_INCR;

    wlcache = realloc(wlcache, wlcachec * sizeof(struct fimwatch));
#if DEBUG
    if (wlcache == NULL) {
        perror("realloc");
    }
#endif

    for (i = wlcachec - ALLOC_INCR; i < wlcachec; ++i) {
        mark_cache_slot_empty(i);
    }

    // return first slot in newly allocated space
    return wlcachec - ALLOC_INCR;
}

/**
 * add an item to the cache
 */
int add_watch_to_cache(int wd, const char *path, uint32_t mask, bool recursive) {
    int slot = find_empty_cache_slot();
    wlcache[slot].wd = wd;
    strncpy(wlcache[slot].path_name, path, PATH_MAX);
    wlcache[slot].event_mask = mask;
    wlcache[slot].recursive = recursive;
    return slot;
}

/**
 * return the cache slot that corresponds to a particular path name
 * or -1 if the path is not in the cache
 */
int path_name_to_cache_slot(const char *path) {
    int i;
    for (i = 0; i < wlcachec; ++i) {
        if (wlcache[i].wd > -1 &&
            strcmp(wlcache[i].path_name, path) == 0) {
            return i;
        }
    }
    return -1;
}
