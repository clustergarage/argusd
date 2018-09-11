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
 * deallocate the watch cache
 */
void free_cache(struct fimwatch *watch) {
    if (watch->slot == -1) {
        return;
    }
    // @TODO: document this
    free(watch->wd);
    free(watch->paths);
    watch->pathc = 0;
    mark_cache_slot_empty(watch->slot);
}

/**
 * check that all path names in the cache are valid and refer to directories
 */
void check_cache_consistency(const struct fimwatch *watch) {
    struct stat sb;
    int i, j;

    for (i = 0; i < wlcachec; ++i) {
        if (wlcache[i].pathc == -1) {
            continue;
        }

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
                fprintf(stderr, "check_cache_consistency: %s is not a directory\n", wlcache[i].paths[j]);
#endif
                remove_item_from_cache(&wlcache[i], j);
                continue;
            }
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
    wlcache[slot].pid = -1;
    wlcache[slot].sid = -1;
    wlcache[slot].slot = -1;
    wlcache[slot].fd = EOF;
    wlcache[slot].rootpathc = -1;
    wlcache[slot].pathc = -1;
    wlcache[slot].event_mask = -1;
    wlcache[slot].only_dir = false;
    wlcache[slot].recursive = false;
}

/**
 * find a free slot in the cache
 */
int find_empty_cache_slot() {
    int i, j;
    for (i = 0; i < wlcachec; ++i) {
        if (wlcache[i].pathc == -1) {
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
 * return the cache slot that corresponds to a particular path name
 * or -1 if the path is not in the cache
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

char *wd_to_path_name(const struct fimwatch *watch, const int wd) {
    int i;
    for (i = 0; i < watch->pathc; ++i) {
        if (watch->wd[i] == wd) {
            return watch->paths[i];
        }
    }
}

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
