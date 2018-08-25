#define _GNU_SOURCE
#include <errno.h>
#include <ftw.h>
#include <limits.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>

#include "fimtree.h"
#include "fimcache.h"

/**
 * duplicate the path names supplied on the command line, perform some sanity
 * checking along the way
 */
void copy_root_paths(int pathc, char *paths[]) {
    int i, j;
    struct stat sb;

    // count the number of root paths and check that the paths are valid
    for (i = 0, rootpathc = 0; i < pathc; ++i, ++rootpathc) {
        // check that command-line arguments are directories
        if (lstat(paths[i], &sb) == EOF) {
#if DEBUG
            fprintf(stderr, "lstat() failed on '%s'\n", paths[i]);
            perror("lstat");
#endif
            return;
        }
        if (!S_ISDIR(sb.st_mode)) {
#if DEBUG
            fprintf(stderr, "'%s' is not a directory\n", paths[i]);
            perror("S_ISDIR");
#endif
            return;
        }
    }

    // create a copy of the root directory path names
    rootpaths = calloc(rootpathc, sizeof(char *));
    if (rootpaths == NULL) {
#if DEBUG
        perror("calloc");
#endif
        return;
    }

    rootstat = calloc(rootpathc, sizeof(struct stat));
    if (rootstat == NULL) {
#if DEBUG
        perror("calloc");
#endif
        return;
    }

    for (i = 0; i < rootpathc; ++i) {
        rootpaths[i] = strdup(paths[i]);
        if (rootpaths[i] == NULL) {
#if DEBUG
            perror("strdup");
#endif
            return;
        }

        // if the same filesystem object appears more than once in the command
        // line, this will cause confusion if we later try to remove an object
        // from the set of root paths; so reject such duplicates now
        // note that we can't just do simple string comparisons of the
        // arguments, since different path strings may refer to the same
        // filesystem object (e.g., "foo" and "./foo")
        // so we use stat() to compare inode numbers and containing device IDs
        if (lstat(paths[i], &rootstat[i]) == EOF) {
#if DEBUG
            perror("lstat");
#endif
        }

        for (j = 0; j < i; ++j) {
            if ((rootstat[j].st_ino == rootstat[j].st_ino) &&
                (rootstat[j].st_dev == rootstat[j].st_dev)) {
#if DEBUG
                printf("Duplicate filesystem objects: %s, %s\n", paths[j], paths[j]);
                fflush(stdout);
                return;
#endif
            }
        }
    }

    ignrootpathc = 0;
}

/**
 * return the address of the element in `rootpaths` that points to a string
 * matching `path`, or NULL if there is no match
 */
char **find_root_path(const char *path) {
    int i;
    for (i = 0; i < rootpathc; ++i) {
        if (rootpaths[i] != NULL &&
            strcmp(path, rootpaths[i]) == 0) {
            return &rootpaths[i];
        }
    }
    return NULL;
}

/**
 * ceased to monitor a root path name (probably because it was renamed)
 * so remove this path from the root path list
 */
void remove_root_path(const char *path) {
    char **p = find_root_path(path);
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

    ++ignrootpathc;

    if (ignrootpathc == rootpathc) {
#if DEBUG
        printf("No more root paths left to monitor; bye!\n");
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
    int wd, slot, flags;
    if (!S_ISDIR(sb->st_mode)) {
        // ignore nondirectory files
        return 0;
    }

	// we need to watch certain events at all times for keeping a consistent
	// view of the filesystem tree
    // @TODO: make this configurable if one wants to watch individual file(s)?
	flags |= IN_ONLYDIR | IN_CREATE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF;
    if (find_root_path(path) != NULL) {
        flags |= IN_MOVE_SELF;
    }

    // make directories for events
    wd = inotify_add_watch(ifd, path, imask | flags);
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
        //} else {
        //  exit(EXIT_FAILURE);
        }
    }

    if (find_watch(wd) > -1) {
        // this watch descriptor is already in the cache
#if DEBUG
        printf("wd: %d already in cache (%s)\n", wd, path);
#endif
        return 0;
    }

    ++wlpathc;

    // cache information about the watch
    slot = add_watch_to_cache(wd, path, imask);

#if DEBUG
    // print the name of the current directory
    printf("    watch_path: wd = %d [cache slot: %d]; %s\n", wd, slot, path);
#endif

    return 0;
}

/**
 * add `path` to the watch list of the inotify file descriptor `fd`
 * the process is recursive: watch items are also created for all of the
 * subdirectories of `path`
 * returns number of watches/cache entries added for this subtree
 */
int watch_path(int fd, const char *path, uint32_t mask) {
    wlpathc = 0;
    ifd = fd;
    imask = mask;

    // use FTW_PHYS to avoid following soft links to directories (which could
    // lead us in circles)
    // by the time we come to process `path`, it may already have been deleted,
    // so we log errors from `nftw`, but keep on going
    if (nftw(path, traverse_tree, 20, FTW_PHYS) == EOF) {
        printf("nftw: %s: %s (directory probably deleted before we could watch)\n", path, strerror(errno));
    }

    return wlpathc;
}

/**
 * add watches and cache entries for a subtree, logging a message noting the
 * number entries added
 */
void watch_subtree(int fd, char *path, uint32_t mask) {
    int cnt = watch_path(fd, path, mask);
#if DEBUG
    printf("    watch_subtree: %s: %d entries added\n", path, cnt);
    fflush(stdout);
#endif
}

/**
 * the directory `oldpathpf`/`oldname` was renamed to
 * `newpathpf`/`newname` fix up cache entries for
 * `oldpathpf`/`oldname` and all of its subdirectories to reflect
 * the change
 */
void rewrite_cached_paths(const char *oldpathpf, const char *oldname, const char *newpathpf, const char *newname) {
    char fullpath[PATH_MAX], newpf[PATH_MAX], newpath[PATH_MAX];
    size_t len;
    int i;

    snprintf(fullpath, sizeof(fullpath), "%s/%s", oldpathpf, oldname);
    snprintf(newpf, sizeof(newpf), "%s/%s", newpathpf, newname);
    len = strlen(fullpath);

#if DEBUG
    printf("rename: %s ==> %s\n", fullpath, newpf);
#endif

    for (i = 0; i < wlcachec; ++i) {
        if (strncmp(fullpath, wlcache[i].path_name, len) == 0 &&
            (wlcache[i].path_name[len] == '/' ||
            wlcache[i].path_name[len] == '\0')) {
            snprintf(newpath, sizeof(newpath), "%s%s", newpf, &wlcache[i].path_name[len]);
            strncpy(wlcache[i].path_name, newpath, PATH_MAX);
#if DEBUG
            printf("    wd %d [cache slot %d] ==> %s\n", wlcache[i].wd, i, newpath);
#endif
        }
    }
}

/**
 * remove watches and cache entries for directory `path` and all of its
 * subdirectories
 * returns number of entries that we (tried to) remove, or -1 if an
 * `inotify_rm_watch` call failed
 */
int remove_subtree(int fd, char *path) {
    size_t len = strlen(path);
    int i;
    int cnt = 0;
    // the argument we receive might be a pointer to a path string that is
    // actually stored in the cache
    // if we remove that path part way through scanning the whole cache then
    // chaos ensues; so, create a temporary copy
    char *pn = strdup(path);

#if DEBUG
    printf("removing subtree: %s\n", path);
#endif

    for (i = 0; i < wlcachec; ++i) {
        if (wlcache[i].wd > -1) {
            if (strncmp(pn, wlcache[i].path_name, len) == 0 &&
                (wlcache[i].path_name[len] == '/' ||
                wlcache[i].path_name[len] == '\0')) {
#if DEBUG
                printf("    removing watch: wd = %d (%s)\n",
                    wlcache[i].wd, wlcache[i].path_name);
                fflush(stdout);
#endif

                if (inotify_rm_watch(fd, wlcache[i].wd) == EOF) {
#if DEBUG
                    printf("inotify_rm_watch wd = %d (%s): %s\n",
                        wlcache[i].wd, wlcache[i].path_name, strerror(errno));
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

    free(pn);
    return cnt;
}
