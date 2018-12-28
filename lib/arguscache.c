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

struct arguswatch **wlcache = NULL;
int wlcachec = 0;

/**
 * Deallocate the watch cache.
 *
 * @param watch
 */
void clear_watch(struct arguswatch **watch) {
    int i;
    if ((*watch)->slot == -1) {
        return;
    }
    // Free up dynamically-allocated memory for `wd` and `paths` arrays.
    for (i = 0; i < (*watch)->pathc; ++i) {
        if ((*watch)->paths[i]) {
            free((*watch)->paths[i]);
        }
    }
    (*watch)->pathc = 0;
    (*watch)->fd = EOF;
    (*watch)->processevtfd = EOF;
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
        // In the case we're still initializing the cache, we want to make sure
        // we don't access unmapped memory.
        if (wlcache[i] == NULL) {
            continue;
        }
        if (wlcache[i]->pid == pid &&
            wlcache[i]->sid == sid) {
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
void check_cache_consistency(struct arguswatch **watch) {
    struct stat sb;
    int i;

    for (i = 0; i < (*watch)->pathc;) {
        if (*(*watch)->paths[i] == '\0') {
            goto out_increaseloop;
        }
        if (lstat((*watch)->paths[i], &sb) == EOF) {
#if DEBUG
            printf("%s: stat: [slot = %d; wd = %d] %s: %s\n", __func__,
                i, (*watch)->wd[i], (*watch)->paths[i], strerror(errno));
            fflush(stdout);
#endif
            remove_item_from_cache(watch, i);
            continue;
        }

        if ((*watch)->only_dir &&
            !S_ISDIR(sb.st_mode)) {
#if DEBUG
            fprintf(stderr, "%s: %s is not a directory\n", __func__,
                (*watch)->paths[i]);
#endif
            remove_item_from_cache(watch, i);
            continue;
        }

out_increaseloop:
        ++i;
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
static void remove_item_from_cache(struct arguswatch **watch, const int index) {
    int i;
    for (i = index; i < (*watch)->pathc - 1; ++i) {
        (*watch)->wd[i] = (*watch)->wd[i + 1];
        free((*watch)->paths[i]);
        (*watch)->paths[i] = strdup((*watch)->paths[i + 1]);
    }
    if ((*watch)->pathc) {
        free((*watch)->paths[--(*watch)->pathc]);
    }
}

/**
 * Check whether the cache contains the watch descriptor `wd`. If found, return
 * the slot number, otherwise return -1.
 *
 * @param watch
 * @param wd
 * @return
 */
int find_watch(const struct arguswatch *const watch, const int wd) {
    int i;
    if (watch->slot == -1) {
        return -1;
    }
    for (i = 0; i < watch->pathc; ++i) {
        if (watch->wd[i] == wd) {
            return i;
        }
    }
    return -1;
}

/**
 * Finds `watch` corresponding to watch descriptor `wd` in the cache.
 *
 * @param watch
 * @param wd
 * @return
 */
int find_watch_checked(const struct arguswatch *const watch, const int wd) {
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
    struct arguswatch *watch;
    if ((watch = calloc(1, sizeof(struct arguswatch))) == NULL) {
#if DEBUG
        perror("calloc");
#endif
        return;
    }
    // Placeholder to pick open slot.
    watch->slot = -1;
    wlcache[slot] = watch;
}

/**
 * Find a free slot in the cache.
 *
 * @return
 */
static int find_empty_cache_slot() {
    int i, len;
    for (i = 0; i < wlcachec; ++i) {
        if (wlcache[i]->slot == -1) {
            return i;
        }
    }

    // No free slot found; resize cache.
    len = wlcachec + ALLOC_INC;

    if ((wlcache = realloc(wlcache, len * sizeof(struct arguswatch *))) == NULL) {
#if DEBUG
        perror("realloc");
#endif
        return -1;
    }

    for (i = len - ALLOC_INC; i < len; ++i, ++wlcachec) {
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
void add_watch_to_cache(struct arguswatch **watch) {
    int slot = find_empty_cache_slot();
    (*watch)->slot = slot;
    // Point this `wlcache` slot to `watch`.
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
int path_name_to_cache_slot(const struct arguswatch *const watch, const char *const path) {
    int i;
    if (watch->slot == -1) {
        return -1;
    }
    for (i = 0; i < watch->pathc; ++i) {
        if (strcmp(watch->paths[i], path) == 0) {
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
const char *wd_to_path_name(const struct arguswatch *const watch, const int wd) {
    int i;
    for (i = 0; i < watch->pathc; ++i) {
        if (watch->wd[i] == wd) {
            return watch->paths[i];
        }
    }
    return "";
}
