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

extern struct fimwatch *wlcache;

/**
 * deallocate the watch cache
 */
void free_cache(struct fimwatch *watch) {
    if (watch->slot == -1) {
        return;
    }
    // free up dynamically-allocated memory for `wd` and `paths` arrays
    free(watch->wd);
    free(watch->paths);
    mark_cache_slot_empty(watch->slot);
}

/**
 * find the position in the `wlcache` given a `pid` and `sid`
 */
int find_cached_slot(const int pid, const int sid) {
    int i;
    for (i = 0; i < wlcachec; ++i) {
        if (wlcache[i].pid == pid &&
            wlcache[i].sid == sid) {
            return i;
        }
    }
    return -1;
}

/**
 * check that all path names in the cache are valid and refer to directories
 */
void check_cache_consistency(const struct fimwatch *watch) {
    struct stat sb;
    int i, j;

    for (i = 0; i < wlcachec; ++i) {
        for (j = 0; j < wlcache[i].pathc; ++j) {
            if (lstat(wlcache[i].paths[j], &sb) == EOF) {
#if DEBUG
                printf("check_cache_consistency: stat: [slot = %d; wd = %d] %s: %s\n",
                    i, wlcache[i].wd[j], wlcache[i].paths[j], strerror(errno));
                fflush(stdout);
#endif
                remove_item_from_cache(&wlcache[i], j);
                continue;
            }

            if (watch->only_dir &&
                !S_ISDIR(sb.st_mode)) {
#if DEBUG
                fprintf(stderr, "check_cache_consistency: %s is not a directory\n",
                    wlcache[i].paths[j]);
#endif
                remove_item_from_cache(&wlcache[i], j);
                continue;
            }
        }
    }
}

/**
 * when checking cache consistency, remove an item at `index` in a given
 * fimwatch object
 * this just moves the `wd` and `paths` position in the watch object, doesn't
 * deallocate any memory or remove the item itself from the `wlcache`
 */
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
int find_watch(const struct fimwatch *watch, const int wd) {
    int i;
    if (watch->slot == -1) {
        return -1;
    }
    for (i = 0; i < wlcache[watch->slot].pathc; ++i) {
        if (wlcache[watch->slot].wd[i] == wd) {
            return i;
        }
    }
    return -1;
}

/**
 * deallocate the watch cache
 */
int find_watch_checked(const struct fimwatch *watch, const int wd) {
    int slot = find_watch(watch, wd);
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
void mark_cache_slot_empty(const int slot) {
    wlcache[slot] = (struct fimwatch){
        .pid = -1,
        .sid = -1,
        .slot = -1,
        .fd = EOF,
        .rootpathc = 0,
        .pathc = 0,
        .event_mask = -1,
        .only_dir = false,
        .recursive = false
    };
}

/**
 * find a free slot in the cache
 */
int find_empty_cache_slot() {
    int i, j;
    for (i = 0; i < wlcachec; ++i) {
        if (wlcache[i].slot == -1) {
            return i;
        }
    }
    // no free slot found; resize cache
    wlcachec += ALLOC_INC;

    wlcache = realloc(wlcache, wlcachec * sizeof(struct fimwatch));
#if DEBUG
    if (wlcache == NULL) {
        perror("realloc");
    }
#endif

    for (i = wlcachec - ALLOC_INC; i < wlcachec; ++i) {
        mark_cache_slot_empty(i);
    }
    // return first slot in newly allocated space
    return wlcachec - ALLOC_INC;
}

/**
 * add an item to the cache
 */
void add_watch_to_cache(struct fimwatch *watch) {
    int slot = find_empty_cache_slot();
    watch->slot = slot;
    wlcache[slot] = *watch;
}

/**
 * return the cache slot that corresponds to a particular path name or -1 if
 * the path is not in the cache
 */
int path_name_to_cache_slot(const struct fimwatch *watch, const char *path) {
    int i;
    if (watch->slot == -1 ||
        wlcache[watch->slot].pathc == -1) {
        return -1;
    }
    for (i = 0; i < wlcache[watch->slot].pathc; ++i) {
        if (strcmp(wlcache[watch->slot].paths[i], path) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * return the pathname that corresponds to the watch descriptor `wd` or blank
 * string if the watch descriptor is not in the cache
 */
char *wd_to_path_name(const struct fimwatch *watch, const int wd) {
    int i;
    for (i = 0; i < watch->pathc; ++i) {
        if (watch->wd[i] == wd) {
            return watch->paths[i];
        }
    }
    return "";
}

/**
 * return the cache slot that corresponds to the watch descriptor `wd` or -1 if
 * the watch descriptor is not in the cache
 */
int wd_to_cache_slot(const struct fimwatch *watch, const int wd) {
    int i;
    if (watch->slot == -1) {
        return -1;
    }
    for (i = 0; i < wlcache[watch->slot].pathc; ++i) {
        if (wlcache[watch->slot].wd[i] == wd) {
            return i;
        }
    }
    return -1;
}
