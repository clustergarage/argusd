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
void copy_root_paths(const int pid, int pathc, char *paths[]) {
    int i, j;
    struct stat sb;

    // count the number of root paths and check that the paths are valid
    for (i = 0, rootpathc[pid] = 0; i < pathc; ++i, ++rootpathc[pid]) {
        // check the paths are directories
        if (lstat(paths[i], &sb) == EOF) {
#if DEBUG
            fprintf(stderr, "`lstat` failed on '%s'\n", paths[i]);
            perror("lstat");
#endif
            continue;
        }

#if ONLY_DIR
        if (!S_ISDIR(sb.st_mode)) {
#if DEBUG
            fprintf(stderr, "'%s' is not a directory\n", paths[i]);
#endif
            continue;
        }
#endif
    }

    // create a copy of the root directory path names
    rootpaths[pid] = calloc(rootpathc[pid], sizeof(char *));
    if (rootpaths[pid] == NULL) {
#if DEBUG
        perror("calloc");
#endif
        return;
    }

    rootstat[pid] = calloc(rootpathc[pid], sizeof(struct stat));
    if (rootstat[pid] == NULL) {
#if DEBUG
        perror("calloc");
#endif
        return;
    }

    for (i = 0; i < rootpathc[pid]; ++i) {
        rootpaths[pid][i] = strdup(paths[i]);
        if (rootpaths[pid][i] == NULL) {
#if DEBUG
            perror("strdup");
#endif
            continue;
        }

        // if the same filesystem object appears more than once in the command
        // line, this will cause confusion if we later try to remove an object
        // from the set of root paths; so reject such duplicates now
        // note that we can't just do simple string comparisons of the
        // arguments, since different path strings may refer to the same
        // filesystem object (e.g., "foo" and "./foo")
        // so we use `stat` to compare i-node numbers and containing device IDs
        if (lstat(paths[i], &rootstat[pid][i]) == EOF) {
#if DEBUG
            perror("lstat");
#endif
        }

        for (j = 0; j < i; ++j) {
            if (rootstat[pid][i].st_ino == rootstat[pid][j].st_ino &&
                rootstat[pid][i].st_dev == rootstat[pid][j].st_dev) {
#if DEBUG
                fprintf(stderr, "duplicate filesystem objects: %s, %s\n", paths[i], paths[j]);
                continue;
#endif
            }
        }
    }

    ignrootpathc[pid] = 0;
}

/**
 * return the address of the element in `rootpaths` that points to a string
 * matching `path`, or NULL if there is no match
 */
char **find_root_path(const int pid, const char *path) {
    int i;
    for (i = 0; i < rootpathc[pid]; ++i) {
        if (rootpaths[pid][i] != NULL &&
            strcmp(path, rootpaths[pid][i]) == 0) {
            return &rootpaths[pid][i];
        }
    }
    return NULL;
}

/**
 * ceased to monitor a root path name (probably because it was renamed)
 * so remove this path from the root path list
 */
void remove_root_path(const int pid, const char *path) {
    char **p = find_root_path(pid, path);
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

    ++ignrootpathc[pid];

    if (ignrootpathc[pid] == rootpathc[pid]) {
#if DEBUG
        printf("no more root paths left to monitor\n");
        fflush(stdout);
#endif
    }
}

/**
 * function called by `nftw` to traverse a directory tree that adds a watch
 * for each directory in the tree
 * each successful call to this function should return 0 to indicate to `nftw`
 * that the tree traversal should continue
 */
int traverse_tree(const char *path, const struct stat *sb, int tflag, struct FTW *ftwbuf) {
#if ONLY_DIR
    if (!S_ISDIR(sb->st_mode)) {
        // ignore nondirectory files
        return 0;
    }
#endif
#if DEBUG
        printf("    traverse_tree: %s\n", path);
        fflush(stdout);
#endif
    return watch_path(path);
}

/**
 * add `path` to the watch list of the inotify file descriptor `ifd`
 * the process is not recursive
 * returns number of watches/cache entries added for this subtree
 */
int watch_path(const char *path) {
    int wd, slot, flags;
    // we need to watch certain events at all times for keeping a consistent
    // view of the filesystem tree
#if ONLY_DIR
    flags |= IN_ONLYDIR;
#endif
    flags |= IN_CREATE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF;

    // @TODO: follow symlinks properly
    if (find_root_path(ipid, path) != NULL) {
        flags |= IN_MOVE_SELF;
    }

    // make directories for events
    wd = inotify_add_watch(iwatch->fd, path, iwatch->event_mask | flags);
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
            //exit(EXIT_FAILURE);
            return -1;
        }
    }

#if DEBUG
    if (find_watch(ipid, wd) > -1) {
        // this watch descriptor is already in the cache
        printf("wd: %d already in cache (%s)\n", wd, path);
        fflush(stdout);
    }
#endif

    iwatch->wd[ipathc] = wd;
    iwatch->paths = realloc(iwatch->paths, (ipathc + 1) * sizeof(char *));
#if DEBUG
    if (iwatch->paths == NULL) {
        perror("realloc");
    }
#endif
    iwatch->paths[ipathc] = strdup(path);
    ++ipathc;

    return 0;
}

/**
 * add `path` to the watch list of the inotify file descriptor `ifd`
 * the process is recursive: watch items are also created for all of the
 * subdirectories of `path`
 * returns number of watches/cache entries added for this subtree
 */
int watch_path_recursive(const char *path) {
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
    return ipathc;
}

/**
 * add watches and cache entries for a subtree, logging a message noting the
 * number entries added
 */
void watch_subtree(const int pid, struct fimwatch *watch) {
    int i;
    int pathc = rootpathc[pid];
    char **paths = calloc(pathc, sizeof(char *));
#if DEBUG
    if (paths == NULL) {
        perror("calloc");
    }
#endif
    for (i = 0; i < pathc; ++i) {
        paths[i] = strdup(rootpaths[pid][i]);
    }

    ipid = pid;
    ipathc = 0;
    iwatch = malloc(sizeof(struct fimwatch));
#if DEBUG
    if (iwatch == NULL) {
        perror("malloc");
    }
#endif
    *iwatch = *watch;

    for (i = 0; i < pathc; ++i) {
        if (watch->recursive) {
            watch_path_recursive(paths[i]);
        } else {
            watch_path(paths[i]);
        }
#if DEBUG
        printf("    watch_subtree: %s: %d entries added\n", paths[i], ipathc);
        fflush(stdout);
#endif
    }

    // deep copy watch object
    watch->pathc = ipathc;
    watch->paths = calloc(watch->pathc, sizeof(char *));
#if DEBUG
    if (watch->paths == NULL) {
        perror("calloc");
    }
#endif
    for (i = 0; i < watch->pathc; ++i) {
        watch->wd[i] = iwatch->wd[i];
        watch->paths[i] = strdup(iwatch->paths[i]);
    }

    // cache information about the watch
    add_watch_to_cache(pid, watch);

    // free iwatch memory
    for (i = 0; i < ipathc; ++i) {
        free(iwatch->paths[i]);
    }
    free(iwatch);
    // free paths memory
    for (i = 0; i < pathc; ++i) {
        free(paths[i]);
    }
    free(paths);
}

/**
 * the directory `oldpathpf`/`oldname` was renamed to
 * `newpathpf`/`newname` fix up cache entries for
 * `oldpathpf`/`oldname` and all of its subdirectories to reflect
 * the change
 */
void rewrite_cached_paths(const int pid, const char *oldpathpf, const char *oldname, const char *newpathpf, const char *newname) {
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

    for (i = 0; i < wlcachec[pid]; ++i) {
        for (j = 0; j < wlcache[pid][i].pathc; ++j) {
            if (strncmp(fullpath, wlcache[pid][i].paths[j], len) == 0 &&
                (wlcache[pid][i].paths[j][len] == '/' ||
                wlcache[pid][i].paths[j][len] == '\0')) {
                snprintf(newpath, sizeof(newpath), "%s%s", newpf, &wlcache[pid][i].paths[j][len]);
                wlcache[pid][i].paths[j] = strdup(newpath);
#if DEBUG
                printf("    wd %d [cache slot %d] ==> %s\n", wlcache[pid][i].wd[j], i, newpath);
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
int remove_subtree(const int pid, int fd, char *path) {
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

    for (i = 0; i < wlcachec[pid]; ++i) {
        if (wlcache[pid][i].pathc > -1) {
            for (j = 0; j < wlcache[pid][i].pathc; ++j) {
                if (strncmp(pn, wlcache[pid][i].paths[j], len) == 0 &&
                    (wlcache[pid][i].paths[j][len] == '/' ||
                    wlcache[pid][i].paths[j][len] == '\0')) {
#if DEBUG
                    printf("    removing watch: wd = %d (%s)\n",
                        wlcache[pid][i].wd[j], wlcache[pid][i].paths[j]);
                    fflush(stdout);
#endif

                    if (inotify_rm_watch(fd, wlcache[pid][i].wd[j]) == EOF) {
#if DEBUG
                        printf("inotify_rm_watch wd = %d (%s): %s\n", wlcache[pid][i].wd[j],
                            wlcache[pid][i].paths[j], strerror(errno));
                        fflush(stdout);
#endif

                        // when we have multiple renamers, sometimes
                        // `inotify_rm_watch` fails
                        // in this case, we force a cache rebuild by returning -1
                        // @TODO: is there a better solution?
                        cnt = -1;
                        break;
                    }

                    mark_cache_slot_empty(pid, i);
                    ++cnt;
                }
            }
        }
    }

    free(pn);
    return cnt;
}
