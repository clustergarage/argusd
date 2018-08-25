#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <memory.h>
#include <mqueue.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>

#include "fimnotify.h"

static const int INOTIFY_READ_BUF_LEN = (100 * (sizeof(struct inotify_event) + NAME_MAX + 1));

struct watch *wlcache = NULL; // array of cached items
static int wlcachec = 0;      // current size of the array

/**
 * deallocate the watch cache
 */
static void free_cache() {
    free(wlcache);
    wlcachec = 0;
    wlcache = NULL;
}

/**
 * check that all path names in the cache are valid and refer to directories
 */
static void check_cache_consistency() {
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
				fprintf(stderr, "check_cache_consistency: %s is not a directory\n", wlcache[i].path_name);
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
static int find_watch(int wd) {
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
static int find_watch_checked(int wd) {
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
static void mark_cache_slot_empty(int slot) {
#if DEBUG
    printf("        mark_cache_slot_empty: slot = %d;  wd = %d; path = %s\n",
		slot, wlcache[slot].wd, wlcache[slot].path_name);
	fflush(stdout);
#endif
    wlcache[slot].wd = -1;
    wlcache[slot].path_name[0] = '\0';
    wlcache[slot].event_mask = -1;
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

    wlcache = realloc(wlcache, wlcachec * sizeof(struct watch));
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
static int add_watch_to_cache(int wd, const char *path_name, uint32_t event_mask) {
    int slot = find_empty_cache_slot();
    wlcache[slot].wd = wd;
    strncpy(wlcache[slot].path_name, path_name, PATH_MAX);
    wlcache[slot].event_mask = event_mask;
    return slot;
}

/**
 * return the cache slot that corresponds to a particular path name
 * or -1 if the path is not in the cache
 */
static int path_name_to_cache_slot(const char *path_name) {
    int i;
    for (i = 0; i < wlcachec; ++i) {
        if (wlcache[i].wd > -1 &&
			strcmp(wlcache[i].path_name, path_name) == 0) {
            return i;
		}
	}
    return -1;
}

///////////////////////////////////////////////////////////////////////////////

static char **root_paths;           // list of path names supplied on command line
static int root_pathc;              // number of path names supplied on command line
static int ignored_root_pathc;      // number of command-line path names that we've ceased to monitor
static struct stat *root_path_stat; // stat(2) structures for root directories

/**
 * duplicate the path names supplied on the command line, perform some sanity
 * checking along the way
 */
static void copy_root_paths(int pathc, char *paths[]) {
    //char **p = paths;
    int i, j;
    struct stat sb;

    // count the number of root paths and check that the paths are valid
    //for (p = paths, root_pathc = 0; *p != NULL; ++p, ++root_pathc) {
    for (i = 0, root_pathc = 0; i < pathc; ++i, ++root_pathc) {
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
    root_paths = calloc(root_pathc, sizeof(char *));
    if (root_paths == NULL) {
#if DEBUG
        perror("calloc");
#endif
		return;
	}

    root_path_stat = calloc(root_pathc, sizeof(struct stat));
    if (root_path_stat == NULL) {
#if DEBUG
        perror("calloc");
#endif
		return;
	}

    for (i = 0; i < root_pathc; ++i) {
        root_paths[i] = strdup(paths[i]);
        if (root_paths[i] == NULL) {
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
        if (lstat(paths[i], &root_path_stat[i]) == EOF) {
#if DEBUG
            perror("lstat");
#endif
		}

        for (j = 0; j < i; ++j) {
            if ((root_path_stat[j].st_ino == root_path_stat[j].st_ino) &&
                (root_path_stat[j].st_dev == root_path_stat[j].st_dev)) {
#if DEBUG
                printf("Duplicate filesystem objects: %s, %s\n", paths[j], paths[j]);
				fflush(stdout);
				return;
#endif
            }
        }
    }

    ignored_root_pathc = 0;
}

/**
 * return the address of the element in `root_paths` that points to a string
 * matching `path`, or NULL if there is no match
 */
static char **find_root_path(const char *path) {
    int i;
    for (i = 0; i < root_pathc; ++i) {
        if (root_paths[i] != NULL &&
			strcmp(path, root_paths[i]) == 0) {
            return &root_paths[i];
		}
	}
    return NULL;
}

/**
 * ceased to monitor a root path name (probably because it was renamed)
 * so remove this path from the root path list
 */
static void remove_root_path(const char *path) {
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

    ++ignored_root_pathc;

    if (ignored_root_pathc == root_pathc) {
#if DEBUG
        printf("No more root paths left to monitor; bye!\n");
		fflush(stdout);
#endif
        return;
    }
}

///////////////////////////////////////////////////////////////////////////////

static int wlpathc;    // count of directories added to watch list
static int ifd;        // inotify file descriptor
static uint32_t imask; // inotify event mask

/**
 * function called by `nftw` to traverse a directory tree that adds a watch
 * for each directory in the tree
 * each successful call to this function should return 0 to indicate to `nftw`
 * that the tree traversal should continue
 */
static int traverse_tree(const char *path, const struct stat *sb, int tflag, struct FTW *ftwbuf) {
    int wd, slot, flags;
    if (!S_ISDIR(sb->st_mode)) {
		// ignore nondirectory files
        return 0;
	}

	// @TODO: make this configurable?
	imask |= IN_ONLYDIR;
    if (find_root_path(path) != NULL) {
        imask |= IN_MOVE_SELF;
	}

    // make directories for events
	wd = inotify_add_watch(ifd, path, imask);
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
		//	exit(EXIT_FAILURE);
		}
	}
    fflush(stdout);

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
static int watch_path(int fd, const char *path, uint32_t event_mask) {
    wlpathc = 0;
    ifd = fd;
	imask = event_mask;

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
static void watch_subtree(int fd, char *path, uint32_t event_mask) {
    int cnt = watch_path(fd, path, event_mask);
#if DEBUG
    printf("    watch_subtree: %s: %d entries added\n", path, cnt);
	fflush(stdout);
#endif
}

/**
 * the directory `old_path_prefix`/`old_name` was renamed to
 * `new_path_prefix`/`new_name` fix up cache entries for
 * `old_path_prefix`/`old_name` and all of its subdirectories to reflect
 * the change
 */
static void rewrite_cached_paths(const char *old_path_prefix, const char *old_name, const char *new_path_prefix, const char *new_name) {
    char full_path[PATH_MAX], new_prefix[PATH_MAX];
    char new_path[PATH_MAX];
    size_t len;
    int i;

    snprintf(full_path, sizeof(full_path), "%s/%s", old_path_prefix, old_name);
    snprintf(new_prefix, sizeof(new_prefix), "%s/%s", new_path_prefix, new_name);
    len = strlen(full_path);

#if DEBUG
    printf("rename: %s ==> %s\n", full_path, new_prefix);
#endif

    for (i = 0; i < wlcachec; ++i) {
        if (strncmp(full_path, wlcache[i].path_name, len) == 0 &&
			(wlcache[i].path_name[len] == '/' ||
			wlcache[i].path_name[len] == '\0')) {
            snprintf(new_path, sizeof(new_path), "%s%s", new_prefix, &wlcache[i].path_name[len]);
            strncpy(wlcache[i].path_name, new_path, PATH_MAX);
#if DEBUG
            printf("    wd %d [cache slot %d] ==> %s\n", wlcache[i].wd, i, new_path);
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
static int remove_subtree(int fd, char *path) {
    size_t len;
    int i, cnt;
    char *pn;

#if DEBUG
    printf("removing subtree: %s\n", path);
#endif

    len = strlen(path);

    // the argument we receive might be a pointer to a path string that is
	// actually stored in the cache
	// if we remove that path part way through scanning the whole cache then
	// chaos ensues; so, create a temporary copy
    pn = strdup(path);

    cnt = 0;

    for (i = 0; i < wlcachec; ++i) {
        if (wlcache[i].wd >= 0) {
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

/**
 * when the cache is in an unrecoverable state, we discard the current
 * inotify file descriptor `old_fd` and create a new one (returned
 * as the function result), and remove and rebuild the cache
 *
 * if `old_fd` is -1, this is the initial build of the cache, or an
 * explicitly requested cache rebuild, so we are a little less verbose,
 * and we reset 'reinitCnt'
 *
 * `event_mask` can be reinitialized this way
 */
static int reinitialize(int old_fd, uint32_t event_mask) {
    static int reinit_cnt;
    int fd, cnt, i;

    if (old_fd > EOF) {
        close(old_fd);

        ++reinit_cnt;
#if DEBUG
        printf("Reinitializing cache and inotify FD (reinitCnt = %d)\n", reinit_cnt);
#endif
    } else {
#if DEBUG
        printf("Initializing cache\n");
#endif
        reinit_cnt = 0;
    }

    fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd == EOF) {
#if DEBUG
        perror("inotify_init1");
#endif
		exit(EXIT_FAILURE);
	}

#if DEBUG
    printf("    new fd = %d\n", fd);
#endif

    free_cache();

    for (i = 0; i < root_pathc; ++i) {
        if (root_paths[i] != NULL) {
            watch_subtree(fd, root_paths[i], event_mask);
		}
	}

    cnt = 0;
    for (i = 0; i < wlcachec; ++i) {
        if (wlcache[i].wd >= 0) {
            ++cnt;
		}
	}

#if DEBUG
    if (old_fd >= 0) {
        printf("Rebuilt cache with %d entries\n", cnt);
	}
#endif

    return fd;
}

///////////////////////////////////////////////////////////////////////////////

static int read_buffer_size = 0;
static int inotify_read_cnt = 0;

static int process_next_inotify_event(int *fd, char *ptr, int len, int first, mqd_t mq) {
    const struct inotify_event *event = (const struct inotify_event *)ptr;
	char full_path[PATH_MAX + NAME_MAX];
    size_t event_len;
    int slot;

    if (event->wd != EOF &&
		!(event->mask & IN_IGNORED)) {
		/* IN_Q_OVERFLOW has (event->wd == -1) */
		/* Skip IN_IGNORED, since it will come after an event that has already zapped the corresponding cache entry */
        /* Cache consistency check; see the discussion of "intra-tree" rename() events */
        slot = find_watch_checked(event->wd);
        if (slot == -1) {
            /* Cache reached an inconsistent state */
            *fd = reinitialize(*fd, wlcache[slot].event_mask);
            /* Discard all remaining events in current read() buffer */
            return INOTIFY_READ_BUF_LEN;
        }
    }

    event_len = sizeof(struct inotify_event) + event->len;

    if ((event->mask & IN_ISDIR) &&
		(event->mask & (IN_CREATE | IN_MOVED_TO))) {
        /* A new subdirectory was created, or a subdirectory was
           renamed into the tree; create watches for it, and all
           of its subdirectories. */
        snprintf(full_path, sizeof(full_path), "%s/%s",
			wlcache[slot].path_name, event->name);

#if DEBUG
        printf("Directory creation on wd %d: %s\n", event->wd, full_path);
#endif

        /*
		We only watch the new subtree if it has not already been cached.
		This deals with a race condition:
		* On the one hand, the following steps might occur:
			1. The "child" directory is created.
			2. The "grandchild" directory is created
			3. We receive an IN_CREATE event for the creation of the
				"child" and create a watch and a cache entry for it.
			4. To handle the possibility that step 2 came before
				step 3, we recursively walk through the descendants of
				the "child" directory, adding any subdirectories to
				the cache.
		* On the other hand, the following steps might occur:
			1. The "child" directory is created.
			3. We receive an IN_CREATE event for the creation of the
				"child" and create a watch and a cache entry for it.
			3. The "grandchild" directory is created
			4. During the recursive walk through the descendants of
				the "child" directory, we cache the "grandchild" and
				add a watch for it.
			5. We receive the IN_CREATE event for the creation of
				the "grandchild". At this point, we should NOT create
				a cache entry and watch for the "grandchild" because
				they already exist. (Creating the watch for the
				second time is harmless, but adding a second cache
				for the grandchild would leave the cache in a confused
				state.)
		*/
        if (!path_name_to_cache_slot(full_path) > -1) {
			slot = find_watch_checked(event->wd);
			if (slot > -1) {
				watch_subtree(*fd, full_path, wlcache[slot].event_mask);
			}
		}
    } else if (event->mask & IN_DELETE_SELF) {
        // A directory was deleted. Remove the corresponding item from the cache.
#if DEBUG
        printf("Clearing watchlist item %d (%s)\n", event->wd, wlcache[slot].path_name);
#endif

        if (find_root_path(wlcache[slot].path_name) != NULL) {
            remove_root_path(wlcache[slot].path_name);
		}

        mark_cache_slot_empty(slot);
		// No need to remove the watch; that happens automatically
    } else if ((event->mask & (IN_MOVED_FROM | IN_ISDIR)) == (IN_MOVED_FROM | IN_ISDIR)) {
        /*
		We have a "moved from" event. To know how to deal with it, we
		need to determine whether there is a following "moved to"
		event with a matching cookie value (i.e., an "intra-tree"
		rename() where the source and destination are inside our
		monitored trees).  If there is not, then we are dealing
		with a rename() out of our monitored tree(s).

		We assume that if this is an "intra-tree" rename() event, then
		the "moved to" event is the next event in the buffer returned
		by the current read(). (If we are already at the last event in
		this buffer, then we ask our caller to read a bit more, in
		the hope of getting the following IN_MOVED_TO event in the
		next read().)

		In most cases, the assumption holds. However, where multiple
		processes are manipulating the tree, we can can get event
		sequences such as the following:

			IN_MOVED_FROM (rename(x) by process A)
					IN_MOVED_FROM (rename(y) by process B)
					IN_MOVED_TO   (rename(y) by process B)
			IN_MOVED_TO   (rename(x) by process A)

		In principle, there may be arbitrarily complex variations on
		the above theme. Our assumption that related IN_MOVED_FROM
		and IN_MOVED_TO events are consecutive is broken by such
		scenarios.

		We could try to resolve this issue by extending the window
		we use to search for IN_MOVED_TO events beyond the next item
		in the queue. But this must be done heuristically (e.g.,
		limiting the window to N events or to events read within
		X milliseconds), because sometimes we will have an unmatched
		IN_MOVED_FROM events that result from out-of-tree renames.
		The heuristic approach is therefore unavoidably racy: there
		is always a chance that we will fail to match up an
		IN_MOVED_FROM+IN_MOVED_TO event pair.

		So, this program takes the simple approach of assuming
		that an IN_MOVED_FROM+IN_MOVED_TO pair occupy consecutive
		events in the buffer returned by read().

		When that assumption is wrong (and we therefore fail
		to recognize an intra-tree rename() event), then
		the rename will be treated as separate "moved from" and
		"moved to" events, with the result that some watch items
		and cache entries are zapped and re-created. This causes
		the watch descriptors in our cache to become inconsistent
		with the watch descriptors in as yet unread events,
		because the watches are re-created with different watch
		descriptor numbers.

		Once such an inconsistency occurs, then, at some later point,
		we will do a lookup for a watch descriptor returned by
		inotify, and find that it is not in our cache. When that
		happens, we reinitialize our cache with a fresh set of watch
		descriptors and re-create the inotify file descriptor, in
		order to bring our cache back into consistency with the
		filesystem. An alternative would be to cache the cookies of
		the (recent) IN_MOVED_FROM events for which which we did not
		find a matching IN_MOVED_TO event, and rebuild our watch
		cache when we find an IN_MOVED_TO event whose cookie matches
		one of the cached cookies. Yet another approach when we
		detect an out-of-tree rename would be to reinitialize the
		cache and create a new inotify file descriptor.
		(TODO: consider the fact that for a rename event, there
		won't be other events for the object between IN_MOVED_FROM
		and IN_MOVED_TO.)

		Rebuilding the watch cache is expensive if the monitored
		tree is large. So, there is a trade-off between how much
		effort we want to go to to avoid cache rebuilds versus
		how much effort we want to devote to matching up
		IN_MOVED_FROM+IN_MOVED_TO event pairs. At the one extreme
		we would do no search ahead for IN_MOVED_TO, with the result
		that every rename() potentially could trigger a cache
		rebuild. Limiting the search window to just the following
		event is a compromise that catches the vast majority of
		intra-tree renames and triggers relatively few cache rebuilds.
        */

		const struct inotify_event *next_event = (const struct inotify_event *)(ptr + event_len);

        if (((char *)next_event < ptr + len) &&
			(next_event->mask & IN_MOVED_TO) &&
			(next_event->cookie == event->cookie)) {
            /* We have a rename() event. We need to fix up the
               cached pathnames for the corresponding directory
               and all of its subdirectories. */
            slot = find_watch_checked(next_event->wd);
            if (slot > -1) {
                /* Cache reached an inconsistent state */
                *fd = reinitialize(*fd, wlcache[slot].event_mask);
                /* Discard all remaining events in current read() buffer */
                return INOTIFY_READ_BUF_LEN;
            }

            rewrite_cached_paths(wlcache[slot].path_name, event->name,
				wlcache[slot].path_name, next_event->name);

            /* We have also processed the next (IN_MOVED_TO) event,
               so skip over it */
            event_len += sizeof(struct inotify_event) + next_event->len;
        } else if (((char *)next_event < ptr + len) || !first) {
            /* We got a "moved from" event without an accompanying
               "moved to" event. The directory has been moved
               outside the tree we are monitoring. We need to
               remove the watches and zap the cache entries for
               the moved directory and all of its subdirectories. */
#if DEBUG
            printf("MOVED_OUT: %p %p\n", wlcache[slot].path_name, event->name);
            printf("firstTry = %d; remaining bytes = %ld\n", first, ptr + len - (char *)next_event);
#endif
            snprintf(full_path, sizeof(full_path), "%s/%s", wlcache[slot].path_name, event->name);

            slot = find_watch_checked(event->wd);
            if (slot > -1 &&
				remove_subtree(*fd, full_path) == -1) {
                /* Cache reached an inconsistent state */
                *fd = reinitialize(*fd, wlcache[slot].event_mask);
                /* Discard all remaining events in current read() buffer */
                return INOTIFY_READ_BUF_LEN;
            }
        } else {
#if DEBUG
            printf("HANGING IN_MOVED_FROM\n");
#endif
			// tell our caller to do another `read`
            return -1;
        }
    } else if (event->mask & IN_Q_OVERFLOW) {
        static int overflow_cnt = 0;
        ++overflow_cnt;

#if DEBUG
        printf("Queue overflow (%d) (inotifyReadCnt = %d)\n", overflow_cnt, inotify_read_cnt);
#endif

        /* When the queue overflows, some events are lost, at which
           point we've lost any chance of keeping our cache consistent
           with the state of the filesystem. So, discard this inotify
           file descriptor and create a new one, and zap and rebuild
           the cache. */
		slot = find_watch_checked(event->wd);
		if (slot > -1) {
			*fd = reinitialize(*fd, wlcache[slot].event_mask);
		}
        /* Discard all remaining events in current read() buffer */
        event_len = INOTIFY_READ_BUF_LEN;
    } else if (event->mask & IN_UNMOUNT) {
        /* When a filesystem is unmounted, each of the watches on the
           is dropped, and an unmount and an ignore event are generated.
           There's nothing left for us to monitor, so we just zap the
           corresponding cache entry. */

#if DEBUG
        printf("Filesystem unmounted: %s\n", wlcache[slot].path_name);
#endif

        mark_cache_slot_empty(slot);
		/* No need to remove the watch; that happens automatically */
    } else if (event->mask & IN_MOVE_SELF &&
		find_root_path(wlcache[slot].path_name) != NULL) {

        /* If the root path moves to a new location in the same
           filesystem, then all cached pathnames become invalid, and we
           have no direct way of knowing the new name of the root path.
           We could in theory find the new name by caching the i-node of
           the root path on start-up and then trying to find a pathname
           that corresponds to that i-node. Instead, we'll keep things
           simple, and just cease monitoring it. */

#if DEBUG
        printf("Root path moved: %s\n", wlcache[slot].path_name);
#endif

        remove_root_path(wlcache[slot].path_name);

        if (remove_subtree(*fd, wlcache[slot].path_name) == -1) {
            /* Cache reached an inconsistent state */
			slot = find_watch_checked(event->wd);
			if (slot > -1) {
				*fd = reinitialize(*fd, wlcache[slot].event_mask);
			}
            /* Discard all remaining events in current read() buffer */
            return INOTIFY_READ_BUF_LEN;
        }
    } else {
		struct fimwatch_event fwevent = {
			.event_mask = event->mask,
			.path_name = wlcache[slot].path_name,         // name of the watched directory
			.file_name = event->len ? event->name : NULL, // name of the file
			.is_dir = event->mask & IN_ISDIR
		};

#if DEBUG
		printf("[%d]\t%s/%s\t[%d]\n", fwevent.event_mask, fwevent.path_name,
			fwevent.file_name, fwevent.is_dir);
		fflush(stdout);
#endif

		// @TODO: document this
		if (mq_send(mq, (const char *)&fwevent, sizeof(fwevent), 0) == EOF) {
#if DEBUG
			perror("mq_send");
#endif
		}
	}

	check_cache_consistency();

    return event_len;
}

/**
 * read all available inotify events from the file descriptor `fd`
 * `pathc` is the length of `paths`
 * `paths` [0->N-1] is the list of watched directories
 */
static void process_inotify_events(int *fd, mqd_t mq) {
    /**
     * some systems cannot read integer variables if they are not properly aligned
     * on other systems, incorrect alignment may decrease performance
     * hence, the buffer used for reading from the inotify file descriptor should
     * have the same alignment as struct inotify_event
     */
    char buf[INOTIFY_READ_BUF_LEN] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t len, nr;
    int i, event_len, slot;
    int first = 1;
    char *ptr;

	len = read(*fd, buf, (read_buffer_size > 0 ? read_buffer_size : INOTIFY_READ_BUF_LEN));
	if (len == EOF) {
#if DEBUG
		perror("read");
#endif
		return;
	} else if (len == 0) {
#if DEBUG
		fprintf(stderr, "read() from inotify fd returned 0!");
#endif
		//exit(EXIT_FAILURE);
		return;
	}

	++inotify_read_cnt;

	// process each event in the buffer returned by `read`
	// loop over all events in the buffer
	for (ptr = buf; ptr < buf + len; /*ptr += sizeof(struct inotify_event) + event->len*/) {
		event_len = process_next_inotify_event(fd, ptr, buf + len - ptr, first, mq);

		if (event_len > 0) {
			ptr += event_len;
			first = 1;
		} else {
			/* We got here because an IN_MOVED_FROM event was found at
			the end of a previously read buffer and that event may be
			part of an "intra-tree" rename(), meaning that we should
			check if there is a subsequent IN_MOVED_TO event with the
			same cookie value. We left that event unprocessed and we
			will now try to read some more events, delaying for a
			short time, to give the associated IN_MOVED_IN event (if
			there is one) a chance to arrive. However, we only want
			to do this once: if the read() below fails to gather
			further events, then when we reprocess the IN_MOVED_FROM
			we should treat it as though this is an out-of-tree
			rename(). Thus, we set 'firstTry' to 0 for the next
			processNextInotifyEvent() call. */

			int saved_errno;
			first = 0;
			len = buf + len - ptr;

			/* Shuffle remaining bytes to start of buffer */
			for (i = 0; i < len; ++i) {
				buf[i] = ptr[i];
			}

			/* Set a timeout for read(). Some rough testing suggests
			that a 2-millisecond timeout is sufficient to ensure
			that, in around 99.8% of cases, we get the IN_MOVED_TO
			event (if there is one) that matched an IN_MOVED_FROM
			event, even in a highly dynamic directory tree. This
			number may, of course, warrant tuning on different
			hardware and in environments with different filesystem
			activity levels. */

			ualarm(2000, 0);

			nr = read(*fd, buf + len, INOTIFY_READ_BUF_LEN - len);

			// in case `ualarm` should change errno
			saved_errno = errno;
			// cancel alarm
			ualarm(0, 0);
			errno = saved_errno;

			if (nr == -1 && errno != EINTR) {
#if DEBUG
				perror("read");
#endif
			} else if (nr == 0) {
#if DEBUG
				fprintf(stderr, "read() from inotify fd returned 0!");
#endif
				//exit(EXIT_FAILURE);
				return;
			}

			if (errno != -1) {
				len += nr;
				++inotify_read_cnt;
			} else {
				// EINTR
			}

			// start again at beginning of buffer
			ptr = buf;
		}
	}
}

int start_inotify_watcher(int pathc, char *paths[], uint32_t event_mask, int process_eventfd) {
	// @FIXME: most of these need to be pulled out into static/global vars, other fns use them
    int fd, poll_num;
    mqd_t mq; // @TODO: share same mq instance (static)
    nfds_t nfds;
    struct pollfd fds[2];
    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGCHLD);

	// save a copy of the paths
    copy_root_paths(pathc, &paths[0]);

#if DEBUG
    printf("  Listening for events on:\n");
	fflush(stdout);
#endif

    // create an inotify instance and populate it with entries for paths
	fd = reinitialize(-1, event_mask);
    if (fd == EOF) {
        goto exit;
    }

    // open mqueue
    mq = mq_open(MQ_QUEUE_NAME, O_WRONLY);
    if (mq == EOF) {
#if DEBUG
        perror("mq_open");
#endif
        goto exit;
    }

    // prepare for polling
    nfds = 2;
    // inotify input
    fds[0].fd = fd;
    fds[0].events = POLLIN;
    // anonymous pipe for manual kill
    fds[1].fd = process_eventfd;
    fds[1].events = POLLIN;

    // wait for events
    while (1) {
        poll_num = ppoll(fds, nfds, NULL, &sigmask);
        if (poll_num == EOF) {
            if (errno == EINTR) {
                continue;
            }
#if DEBUG
            perror("poll");
#endif
            goto exit;
        }

        if (poll_num > 0) {
            if (fds[0].revents & POLLIN) {
                // inotify events are available
                process_inotify_events(&fd, /*pathc, paths, */mq);
            }

            if (fds[1].revents & POLLIN) {
                // anonymous pipe events are available
                uint64_t value;
                int len = read(fds[1].fd, &value, sizeof(uint64_t));
                if (len != EOF &&
                    (value & FIMNOTIFY_KILL)) {
                    break;
                }
            }
        }
    }

#if DEBUG
    printf("  Listening for events stopped.\n");
    fflush(stdout);
#endif

exit:
    // close inotify file descriptor
    if (fd != EOF) {
        close(fd);
    }
    // close message queue
    if (mq != EOF) {
        mq_close(mq);
    }

    exit(errno ? EXIT_FAILURE : EXIT_SUCCESS);
}

///////////////////////////////////////////////////////////////////////////////

void join_namespace(const pid_t pid, const char *ns) {
    char file[1024];
    int fd;

    // get file descriptor for namespace
    sprintf(file, "/proc/%d/ns/%s", pid, ns);
    fd = open(file, O_RDONLY | O_CLOEXEC);
    if (fd == EOF) {
#if DEBUG
        perror("open");
#endif
        goto exit;
    }

    // join namespace
    if (setns(fd, CLONE_NEWNS) == EOF) {
#if DEBUG
        fprintf(stderr, "Cannot perform `setns` (%d)\n", errno);
        perror("setns");
#endif
        goto exit;
    }

#if DEBUG
    printf("Joined namespace: %s.\n", file);
    fflush(stdout);
#endif

exit:
    // close namespace file descriptor
    if (fd != EOF) {
        close(fd);
    }
}
