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

#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>

#include "arguscache.h"
#include "argusutil.h"

/**
 * Deallocate the watch cache.
 *
 * @param watch
 */
void free_cache(struct arguswatch *watch) {
    if (watch->slot == -1) {
        return;
    }
    // Free up dynamically-allocated memory for `wd` and `paths` arrays.
    free(watch->wd);
    free(watch->paths);
    mark_cache_slot_empty(watch->slot);
}

/**
 * Find the position in the `wlcache` given a `pid` and `sid`.
 *
 * @param pid
 * @param sid
 * @return
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
 * Check that all path names in the cache are valid and refer to directories.
 *
 * @param watch
 */
void check_cache_consistency(const struct arguswatch *watch) {
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
 * When checking cache consistency, remove an item at `index` in a given
 * arguswatch object. This just moves the `wd` and `paths` position in the watch
 * object, doesn't deallocate any memory or remove the item itself from the
 * `wlcache`.
 *
 * @param watch
 * @param index
 */
void remove_item_from_cache(struct arguswatch *watch, int const index) {
    int i;
    for (i = index; i < watch->pathc - 1; ++i) {
        watch->wd[i] = watch->wd[i + 1];
        watch->paths[i] = watch->paths[i + 1];
    }
    --watch->pathc;
}

/**
 * Check whether the cache contains the watch descriptor `wd`. If found, return
 * the slot number, otherwise return -1.
 *
 * @param watch
 * @param wd
 * @return
 */
int find_watch(const struct arguswatch *watch, const int wd) {
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
 * Deallocate the watch cache.
 *
 * @param watch
 * @param wd
 * @return
 */
int find_watch_checked(const struct arguswatch *watch, const int wd) {
    int slot = find_watch(watch, wd);
    if (slot > -1) {
        return slot;
    }

#if DEBUG
    printf("could not find watch: %d\n", wd);
    fflush(stdout);
#endif
    // Returning -1 to our caller identifies that there's a problem, and the
    // caller should probably trigger a cache rebuild.
    return -1;
}

/**
 * Mark a cache entry as unused.
 *
 * @param slot
 */
void mark_cache_slot_empty(const int slot) {
    wlcache[slot] = (struct arguswatch){
        .pid = -1,
        .sid = -1,
        .slot = -1,
        .fd = EOF,
        .rootpathc = 0,
        .pathc = 0,
        .event_mask = (uint32_t)-1,
        .only_dir = false,
        .recursive = false
    };
}

/**
 * Find a free slot in the cache.
 *
 * @return
 */
int find_empty_cache_slot() {
    int i;
    for (i = 0; i < wlcachec; ++i) {
        if (wlcache[i].slot == -1) {
            return i;
        }
    }
    // No free slot found; resize cache.
    wlcachec += ALLOC_INC;

    wlcache = realloc(wlcache, wlcachec * sizeof(struct arguswatch));
    if (wlcache == NULL) {
#if DEBUG
        perror("realloc");
#endif
        return -1;
    }

    for (i = wlcachec - ALLOC_INC; i < wlcachec; ++i) {
        mark_cache_slot_empty(i);
    }
    // Return first slot in newly allocated space.
    return wlcachec - ALLOC_INC;
}

/**
 * Add an item to the cache.
 *
 * @param watch
 */
void add_watch_to_cache(struct arguswatch *watch) {
    int slot = find_empty_cache_slot();
    watch->slot = slot;
    wlcache[slot] = *watch;
}

/**
 * Return the cache slot that corresponds to a particular path name or -1 if
 * the path is not in the cache.
 *
 * @param watch
 * @param path
 * @return
 */
int path_name_to_cache_slot(const struct arguswatch *watch, const char *path) {
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
 * Return the pathname that corresponds to the watch descriptor `wd` or blank
 * string if the watch descriptor is not in the cache.
 *
 * @param watch
 * @param wd
 * @return
 */
char *wd_to_path_name(const struct arguswatch *watch, const int wd) {
    int i;
    for (i = 0; i < watch->pathc; ++i) {
        if (watch->wd[i] == wd) {
            return watch->paths[i];
        }
    }
    return "";
}

/**
 * Return the cache slot that corresponds to the watch descriptor `wd` or -1 if
 * the watch descriptor is not in the cache.
 *
 * @param watch
 * @param wd
 * @return
 */
int wd_to_cache_slot(const struct arguswatch *watch, const int wd) {
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
