#define _GNU_SOURCE
#include <errno.h>
#include <ftw.h>
#include <limits.h>
#include <memory.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "fimtree.h"
#include "fimcache.h"
#include "fimutil.h"

/**
 * duplicate the path names supplied on the command line, perform some sanity
 * checking along the way
 */
void copy_root_paths(struct fimwatch *watch) {
    int i, j;
    struct stat sb;

    // count the number of root paths and check that the paths are valid
    for (i = 0; i < watch->rootpathc; ++i) {
        // check the paths are directories
        if (lstat(watch->rootpaths[i], &sb) == EOF) {
#if DEBUG
            fprintf(stderr, "`lstat` failed on '%s'\n", watch->rootpaths[i]);
            perror("lstat");
#endif
            continue;
        }

        if (watch->only_dir &&
            !S_ISDIR(sb.st_mode)) {
#if DEBUG
            fprintf(stderr, "'%s' is not a directory\n", watch->rootpaths[i]);
#endif
            continue;
        }
    }

    watch->rootstat = calloc(watch->rootpathc, sizeof(struct stat));
    if (watch->rootstat == NULL) {
#if DEBUG
        perror("calloc");
#endif
        return;
    }

    for (i = 0; i < watch->rootpathc; ++i) {
        // if the same filesystem object appears more than once in the command
        // line, this will cause confusion if we later try to remove an object
        // from the set of root paths; so reject such duplicates now
        // note that we can't just do simple string comparisons of the
        // arguments, since different path strings may refer to the same
        // filesystem object (e.g., "foo" and "./foo")
        // so we use `stat` to compare i-node numbers and containing device IDs
        if (lstat(watch->rootpaths[i], &watch->rootstat[i]) == EOF) {
#if DEBUG
            perror("lstat");
#endif
        }

        for (j = 0; j < i; ++j) {
            if (watch->rootstat[i].st_ino == watch->rootstat[j].st_ino &&
                watch->rootstat[i].st_dev == watch->rootstat[j].st_dev) {
#if DEBUG
                fprintf(stderr, "duplicate filesystem objects: %s, %s\n", watch->rootpaths[i], watch->rootpaths[j]);
                continue;
#endif
            }
        }
    }

    watch->ignored_rootpathc = 0;
}

/**
 * return the address of the element in `rootpaths` that points to a string
 * matching `path`, or NULL if there is no match
 */
char **find_root_path(const struct fimwatch *watch, const char *path) {
    int i;
    for (i = 0; i < watch->rootpathc; ++i) {
        if (watch->rootpaths[i] != NULL &&
            strcmp(path, watch->rootpaths[i]) == 0) {
            return &watch->rootpaths[i];
        }
    }
    return NULL;
}

/**
 * ceased to monitor a root path name (probably because it was renamed)
 * so remove this path from the root path list
 */
void remove_root_path(struct fimwatch *watch, const char *path) {
    char **p = find_root_path(watch, path);
#if DEBUG
    printf("remove_root_path: %s\n", path);
    fflush(stdout);
#endif
    if (p == NULL) {
#if DEBUG
        printf("remove_root_path: path not found!\n");
        fflush(stdout);
#endif
        return;
    }
    *p = NULL;

    ++watch->ignored_rootpathc;
    if (watch->ignored_rootpathc == watch->rootpathc) {
#if DEBUG
        printf("no more root paths left to monitor\n");
        fflush(stdout);
#endif
    }
}

bool should_ignore_path(struct fimwatch *watch, const char *path) {
    struct stat sb;
    int i;

    // check the paths are directories
    if (lstat(path, &sb) == EOF) {
#if DEBUG
        fprintf(stderr, "`lstat` failed on '%s'\n", path);
        perror("lstat");
#endif
        return true;
    }
    // keep if it is a directory
    if (S_ISDIR(sb.st_mode)) {
        return false;
    }

    // if only watching for directories, ignore
    if (watch->only_dir) {
        return true;
    }
    // make sure path is directly in provided rootpaths
    for (i = 0; i < watch->rootpathc; ++i) {
        if (strcmp(path, watch->rootpaths[i]) == 0) {
            return false;
        }
    }

    // if all else fails, ignore by default
    return true;
}

/**
 * add `path` to the watch list of the inotify file descriptor `ifd`
 * the process is not recursive
 * returns number of watches/cache entries added for this subtree
 */
int watch_path(struct fimwatch *watch, const char *path) {
    int wd, slot;

    // dont add non-directories unless directly specified by rootpaths and
    // only_dir flag is false
    if (should_ignore_path(watch, path)) {
#if DEBUG
        printf("file is ignored: %s\n", path);
        fflush(stdout);
#endif
        return 0;
    }

    // @TODO: follow symlinks properly
    // we need to watch certain events at all times for keeping a consistent
    // view of the filesystem tree
    uint32_t flags = IN_CREATE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF;
    if (watch->only_dir) {
        flags |= IN_ONLYDIR;
    }
    if (find_root_path(watch, path) != NULL) {
        flags |= IN_MOVE_SELF;
    }

    // make directories for events
    wd = inotify_add_watch(watch->fd, path, watch->event_mask | flags);
    if (wd == EOF) {
        // by the time we come to create a watch, the directory might already
        // have been deleted or renamed, in which case we'll get an ENOENT
        // error; log the error, but carry on execution
        // other errors are unexpected, and if we hit them, we give up
#if DEBUG
        fprintf(stderr, "inotify_add_watch: %s: %s\n", path, strerror(errno));
        perror("inotify_add_watch");
#endif
        if (errno == ENOENT) {
            return 0;
        } else {
            return -1;
        }
    }

#if DEBUG
    if (find_watch(watch, wd) > -1) {
        // this watch descriptor is already in the cache
        printf("wd: %d already in cache (%s)\n", wd, path);
        fflush(stdout);
        //return 0;
    }
#endif

    watch->wd = realloc(watch->wd, (watch->pathc + 1) * sizeof(int));
#if DEBUG
    if (watch->wd == NULL) {
        perror("realloc");
    }
#endif
    watch->wd[watch->pathc] = wd;

    watch->paths = realloc(watch->paths, (watch->pathc + 1) * sizeof(char *));
#if DEBUG
    if (watch->paths == NULL) {
        perror("realloc");
    }
#endif
    watch->paths[watch->pathc] = strdup(path);

    ++watch->pathc;

    return 0;
}

/**
 * add `path` to the watch list of the inotify file descriptor `ifd`
 * the process is recursive: watch items are also created for all of the
 * subdirectories of `path`
 * returns number of watches/cache entries added for this subtree
 */
int watch_path_recursive(struct fimwatch *watch, const char *path) {
    /**
     * function called by `nftw` to traverse a directory tree that adds a watch
     * for each directory in the tree
     * each successful call to this function should return 0 to indicate to `nftw`
     * that the tree traversal should continue
     */
    int traverse_tree(const char *path, const struct stat *sb, int tflag, struct FTW *ftwbuf) {
        if (watch->only_dir &&
            !S_ISDIR(sb->st_mode)) {
            // ignore nondirectory files
            return 0;
        }
#if DEBUG
        printf("    traverse_tree: %s\n", path);
        fflush(stdout);
#endif
        return watch_path(watch, path);
    }

    // use FTW_PHYS to avoid following soft links to directories (which could
    // lead us in circles)
    // by the time we come to process `path`, it may already have been deleted,
    // so we log errors from `nftw`, but keep on going
    if (nftw(path, traverse_tree, 20, FTW_PHYS) == EOF) {
#if DEBUG
        printf("nftw: %s: %s (directory probably deleted before we could watch)\n", path, strerror(errno));
        fflush(stdout);
#endif
    }
    return watch->pathc;
}

/**
 * add watches and cache entries for a subtree, logging a message noting the
 * number entries added
 */
void watch_subtree(struct fimwatch *watch) {
    int i;
    for (i = 0; i < watch->rootpathc; ++i) {
        if (watch->recursive) {
            watch_path_recursive(watch, watch->rootpaths[i]);
        } else {
            watch_path(watch, watch->rootpaths[i]);
        }
#if DEBUG
        printf("  watch_subtree: %s: %d entries added\n", watch->rootpaths[i], watch->pathc);
        fflush(stdout);
#endif
    }

    /*
    printf("  $$$$ watch subtree:\n");
    printf("    $$   pid = %d\n", watch->pid);
    printf("    $$   sid = %d\n", watch->sid);
    printf("    $$   fd = %d\n", watch->fd);
    printf("    $$   slot = %d\n", watch->slot);
    printf("    $$   rootpathc = %d\n", watch->rootpathc);
    fflush(stdout);
    for (i = 0; i < watch->rootpathc; ++i) {
        printf("     $     rootpaths[%d] = %s\n", i, watch->rootpaths[i]);
        fflush(stdout);
    }
    printf("    $$   pathc = %d\n", watch->pathc);
    for (i = 0; i < watch->pathc; ++i) {
        printf("     $     paths[%d] = %s; wd = %d\n", i, watch->paths[i], watch->wd[i]);
        fflush(stdout);
    }
    printf("    $$   event_mask = %d\n", watch->event_mask);
    printf("    $$   only_dir = %d\n", watch->only_dir);
    printf("    $$   recursive = %d\n", watch->recursive);
    fflush(stdout);
    */
}

/**
 * the directory `oldpathpf`/`oldname` was renamed to
 * `newpathpf`/`newname` fix up cache entries for
 * `oldpathpf`/`oldname` and all of its subdirectories to reflect
 * the change
 */
void rewrite_cached_paths(const struct fimwatch *watch, const char *oldpathpf, const char *oldname, const char *newpathpf, const char *newname) {
    char fullpath[PATH_MAX], newpf[PATH_MAX], newpath[PATH_MAX];
    size_t len;
    int i, j;

    snprintf(fullpath, sizeof(fullpath), "%s/%s", oldpathpf, oldname);
    snprintf(newpf, sizeof(newpf), "%s/%s", newpathpf, newname);
    len = strlen(fullpath);

#if DEBUG
    printf("rename: %s ==> %s\n", fullpath, newpf);
    fflush(stdout);
#endif

    for (i = 0; i < wlcachec; ++i) {
        if (wlcache[i].pid != watch->pid ||
            wlcache[i].sid != watch->sid) {
            continue;
        }
        for (j = 0; j < wlcache[i].pathc; ++j) {
            if (strncmp(fullpath, wlcache[i].paths[j], len) == 0 &&
                (wlcache[i].paths[j][len] == '/' ||
                wlcache[i].paths[j][len] == '\0')) {
                snprintf(newpath, sizeof(newpath), "%s%s", newpf, &wlcache[i].paths[j][len]);
                wlcache[i].paths[j] = strdup(newpath);
#if DEBUG
                printf("    wd %d [cache slot %d] ==> %s\n", wlcache[i].wd[j], i, newpath);
                fflush(stdout);
#endif
            }
        }
    }
}

/**
 * remove watches and cache entries for directory `path` and all of its
 * subdirectories
 * returns number of entries that we (tried to) remove, or -1 if an
 * `inotify_rm_watch` call failed
 */
int remove_subtree(const struct fimwatch *watch, char *path) {
    size_t len = strlen(path);
    int i, j;
    int cnt = 0;
    // the argument we receive might be a pointer to a path string that is
    // actually stored in the cache
    // if we remove that path part way through scanning the whole cache then
    // chaos ensues; so, create a temporary copy
    char *pn = strdup(path);

#if DEBUG
    printf("removing subtree: %s\n", path);
    fflush(stdout);
#endif

    for (i = 0; i < wlcachec; ++i) {
        if (wlcache[i].pid != watch->pid ||
            wlcache[i].sid != watch->sid) {
            continue;
        }
        if (wlcache[i].pathc > -1) {
            for (j = 0; j < wlcache[i].pathc; ++j) {
                if (strncmp(pn, wlcache[i].paths[j], len) == 0 &&
                    (wlcache[i].paths[j][len] == '/' ||
                    wlcache[i].paths[j][len] == '\0')) {
#if DEBUG
                    printf("    removing watch: wd = %d (%s)\n",
                        wlcache[i].wd[j], wlcache[i].paths[j]);
                    fflush(stdout);
#endif

                    if (inotify_rm_watch(watch->fd, wlcache[i].wd[j]) == EOF) {
#if DEBUG
                        printf("inotify_rm_watch wd = %d (%s): %s\n", wlcache[i].wd[j],
                            wlcache[i].paths[j], strerror(errno));
                        fflush(stdout);
#endif

                        // when we have multiple renamers, sometimes
                        // `inotify_rm_watch` fails
                        // in this case, we force a cache rebuild by returning -1
                        // @TODO: is there a better solution?
                        cnt = -1;
                        break;
                    }

                    mark_cache_slot_empty(i);
                    ++cnt;
                }
            }
        }
    }

    free(pn);
    return cnt;
}
