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
#include <ftw.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "argusnotify.h"
#include "arguscache.h"
#include "argustree.h"
#include "argusutil.h"

/**
 * When the cache is in an unrecoverable state, we discard the current
 * `inotify` file descriptor `oldfd` and create a new one (returned as the
 * function result), and remove and rebuild the cache. If `oldfd` is -1, this
 * is the initial build of the cache, or an explicitly requested cache rebuild,
 * so we are a little less verbose. `event_mask` can be reinitialized this way.
 *
 * @param watch
 * @return
 */
static int reinitialize(struct arguswatch *watch) {
    int fd, slot;
    bool rebuild = watch->fd != EOF;

    if (rebuild) {
        close(watch->fd);
    } else {
#if DEBUG
        printf("initializing cache\n");
        fflush(stdout);
#endif
    }

    fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd == EOF) {
#if DEBUG
        perror("inotify_init1");
#endif
        return -1;
    }
    watch->fd = fd;
#if DEBUG
    printf("  new fd = %d\n", fd);
    fflush(stdout);
#endif

    // Free watch cache.
    free_cache(watch);
    // Begin traversing tree, or non-recursive directories.
    watch_subtree(watch);

    slot = find_cached_slot(watch->pid, watch->sid);
    if (slot == -1) {
        // Cache information about the watch.
        add_watch_to_cache(watch);
    }

#if DEBUG
    int i, cnt;
    for (i = 0, cnt = 0; i < wlcachec; ++i) {
        if (wlcache[i].pid != watch->pid ||
            wlcache[i].sid != watch->sid) {
            continue;
        }
        if (wlcache[i].pathc != EOF) {
            cnt += wlcache[i].pathc;
        }
    }
    if (rebuild) {
        printf("rebuilt cache with %d entries\n", cnt);
        fflush(stdout);
    }
#endif

    // Check cache consistency right away, in case there are multiple
    // containers in a single pod that don't have a path on the filesystem that
    // we specified to watch.
    check_cache_consistency(watch);

    return fd;
}

/**
 * Process the next `inotify` event in the buffer specified by `ptr` and `len`.
 * In most cases, a single event is consumed, but if there is an
 * IN_MOVED_FROM+IN_MOVED_TO pair that share a cookie value, both events are
 * consumed returns the number of bytes in the event(s) consumed from `ptr`.
 *
 * @param watch
 * @param ptr
 * @param len
 * @param first
 * @param logfn
 * @return
 */
static size_t process_next_inotify_event(struct arguswatch *watch, const char *ptr, size_t len, bool first,
    void (*logfn)(struct arguswatch_event *)) {

    const struct inotify_event *event = (const struct inotify_event *)ptr;
    char *path = NULL;
    char fullpath[PATH_MAX + NAME_MAX];
    size_t evtlen;
    int slot = -1, wdslot;

    if (event->wd != EOF) {
        path = wd_to_path_name(watch, event->wd);

        if (!(event->mask & IN_IGNORED)) {
            // IN_Q_OVERFLOW has (event->wd == -1). Skip IN_IGNORED, since it
            // will come after an event that has already removed the
            // corresponding cache entry. Cache consistency check. See the
            // discussion of "intra-tree" `rename` events.
            slot = find_watch_checked(watch, event->wd);
            if (slot == -1) {
                // Reinitialize the `inotify` watch.
                watch->fd = EOF;
                // Cache reached an inconsistent state.
                reinitialize(watch);
                // Discard all remaining events in current `read` buffer.
                return INOTIFY_READ_BUF_LEN;
            }
        }
    }

    evtlen = sizeof(struct inotify_event) + event->len;

    if ((event->mask & IN_ISDIR) &&
        (event->mask & (IN_CREATE | IN_MOVED_TO))) {
        // A new subdirectory was created, or a subdirectory was renamed into
        // the tree; create watches for it, and all of its subdirectories.
        FULL_PATH(fullpath, path, event->name);

#if DEBUG
        printf("directory creation on wd %d: %s\n", event->wd, fullpath);
        fflush(stdout);
#endif

        /**
         * We only watch the new subtree if it has not already been cached this
         * deals with a race condition:
         * - On the one hand, the following steps might occur:
         *   1. The "child" directory is created.
         *   2. The "grandchild" directory is created.
         *   3. We receive an IN_CREATE event for the creation of the "child"
         *      and create a watch and a cache entry for it.
         *   4. To handle the possibility that step 2 came before step 3, we
         *      recursively walk through the descendants of the "child"
         *      directory, adding any subdirectories to the cache.
         * - On the other hand, the following steps might occur:
         *   1. The "child" directory is created.
         *   3. We receive an IN_CREATE event for the creation of the "child"
         *      and create a watch and a cache entry for it.
         *   3. The "grandchild" directory is created.
         *   4. During the recursive walk through the descendants of the
         *      "child" directory, we cache the "grandchild" and add a watch
         *      for it.
         *   5. We receive the IN_CREATE event for the creation of the
         *      "grandchild". At this point, we should NOT create a cache entry
         *      and watch for the "grandchild" because they already exist
         *      (creating the watch for the second time is harmless, but adding
         *      a second cache for the grandchild would leave the cache in a
         *      confused state).
         */
        if (path_name_to_cache_slot(watch, fullpath) == -1) {
            wdslot = wd_to_cache_slot(watch, event->wd);
            if (wdslot > -1 &&
                // Only do this if watching recursively.
                watch->recursive) {
                watch->pathc = 0;
                watch_subtree(watch);
                // @TODO: Verify that this works.
                wlcache[watch->slot] = *watch;
            }
        }
    } else if (event->mask & IN_DELETE_SELF) {
        // A directory was deleted. Remove the corresponding item from the
        // cache.
#if DEBUG
        printf("clearing watchlist item %d (%s)\n", event->wd, path);
        fflush(stdout);
#endif
        if (find_root_path(watch, path) != NULL) {
            remove_root_path(watch, path);
        }
        if (slot > -1) {
            mark_cache_slot_empty(slot);
        }
        // No need to remove the watch, that happens automatically.
    } else if ((event->mask & (IN_MOVED_FROM | IN_ISDIR)) == (IN_MOVED_FROM | IN_ISDIR)) {
        /**
         * We have a "moved from" event. To know how to deal with it, we need
         * to determine whether there is a following "moved to" event with a
         * matching cookie value (i.e., an "intra-tree" `rename` where the
         * source and destination are inside our monitored trees). If there is
         * not, then we are dealing with a `rename` out of our monitored
         * tree(s).
         *
         * We assume that if this is an "intra-tree" `rename` event, then the
         * "moved to" event is the next event in the buffer returned by the
         * current `read`. (If we are already at the last event in this buffer,
         * then we ask our caller to read a bit more, in the hope of getting
         * the following IN_MOVED_TO event in the next `read`.)
         *
         * In most cases, the assumption holds. However, where multiple
         * processes are manipulating the tree, we can get event sequences such
         * as the following:
         *
         *   IN_MOVED_FROM   (rename(x) by process A)
         *     IN_MOVED_FROM (rename(y) by process B)
         *     IN_MOVED_TO   (rename(y) by process B)
         *   IN_MOVED_TO     (rename(x) by process A)
         *
         * In principle, there may be arbitrarily complex variations on the
         * above theme. Our assumption that related IN_MOVED_FROM and
         * IN_MOVED_TO events are consecutive is broken by such scenarios.
         *
         * We could try to resolve this issue by extending the window we use to
         * search for IN_MOVED_TO events beyond the next item in the queue.
         * But this must be done heuristically (e.g., limiting the window to N
         * events or to events read within X milliseconds), because sometimes
         * we will have an unmatched IN_MOVED_FROM events that result from
         * out-of-tree renames. The heuristic approach is therefore unavoidably
         * racy: there is always a chance that we will fail to match up an
         * IN_MOVED_FROM+IN_MOVED_TO event pair.
         *
         * So, this program takes the simple approach of assuming that an
         * IN_MOVED_FROM+IN_MOVED_TO pair occupy consecutive events in the
         * buffer returned by `read`.
         *
         * When that assumption is wrong (and we therefore fail to recognize
         * an intra-tree `rename` event), then the rename will be treated as
         * separate "moved from" and "moved to" events, with the result that
         * some watch items and cache entries are removed and re-created.
         * This causes the watch descriptors in our cache to become
         * inconsistent with the watch descriptors in as yet unread events,
         * because the watches are re-created with different watch descriptor
         * numbers.
         *
         * Once such an inconsistency occurs, then, at some later point, we
         * will do a lookup for a watch descriptor returned by `inotify`, and
         * find that it is not in our cache. When that happens, we reinitialize
         * our cache with a fresh set of watch descriptors and re-create the
         * `inotify` file descriptor, in order to bring our cache back into
         * consistency with the filesystem. An alternative would be to cache
         * the cookies of the (recent) IN_MOVED_FROM events for which which we
         * did not find a matching IN_MOVED_TO event, and rebuild our watch
         * cache when we find an IN_MOVED_TO event whose cookie matches one of
         * the cached cookies. Yet another approach when we detect an
         * out-of-tree rename would be to reinitialize the cache and create a
         * new `inotify` file descriptor.
         *
         * @TODO: Consider the fact that for a rename event, there won't be
         * other events for the object between IN_MOVED_FROM and IN_MOVED_TO.
         *
         * Rebuilding the watch cache is expensive if the monitored tree is
         * large. So, there is a trade-off between how much effort we want to
         * go to to avoid cache rebuilds versus how much effort we want to
         * devote to matching up IN_MOVED_FROM+IN_MOVED_TO event pairs. At the
         * one extreme we would do no search ahead for IN_MOVED_TO, with the
         * result that every `rename` potentially could trigger a cache
         * rebuild. Limiting the search window to just the following event is a
         * compromise that catches the vast majority of intra-tree renames and
         * triggers relatively few cache rebuilds.
         */
        const struct inotify_event *nextevent = (const struct inotify_event *)(ptr + evtlen);

        if (((char *)nextevent < ptr + len) &&
            (nextevent->mask & IN_MOVED_TO) &&
            (nextevent->cookie == event->cookie)) {
            // We have a `rename` event. We need to fix up the cached pathnames
            // for the corresponding directory and all of its subdirectories.
            int nextslot = find_watch_checked(watch, nextevent->wd);
            if (nextslot == -1) {
                // Reinitialize the `inotify` watch.
                watch->fd = EOF;
                // Cache reached an inconsistent state.
                reinitialize(watch);
                // Discard all remaining events in current `read` buffer.
                return INOTIFY_READ_BUF_LEN;
            }

            rewrite_cached_paths(watch, path, event->name,
                wd_to_path_name(watch, nextevent->wd), nextevent->name);

            // Also processed the next (IN_MOVED_TO) event, so skip over it.
            evtlen += sizeof(struct inotify_event) + nextevent->len;
        } else if (((char *)nextevent < ptr + len) || !first) {
            // Got a "moved from" event without an accompanying "moved to"
            // event. The directory has been moved outside the tree we are
            // monitoring need to remove the watches and remove the cache
            // entries for the moved directory and all of its subdirectories.
#if DEBUG
            printf("moved out: %p %p\n", path, event->name);
            printf("first = %d; remaining bytes = %ld\n", first, ptr + len - (char *)nextevent);
            fflush(stdout);
#endif
            FULL_PATH(fullpath, path, event->name);

            slot = find_watch_checked(watch, event->wd);
            if (slot > -1 &&
                remove_subtree(watch, fullpath) == -1) {
                // Cache reached an inconsistent state.
                reinitialize(watch);
                // Discard all remaining events in current `read` buffer.
                return INOTIFY_READ_BUF_LEN;
            }
        } else {
#if DEBUG
            printf("hanging IN_MOVED_FROM\n");
            fflush(stdout);
#endif
            // Tell caller to do another `read`.
            return (size_t)-1;
        }
    } else if (event->mask & IN_Q_OVERFLOW) {
        // When the queue overflows, some events are lost, at which point we've
        // lost any chance of keeping our cache consistent with the state of
        // the filesystem. Discard this `inotify` file descriptor and create a
        // new one, and remove and rebuild the cache.
        slot = find_watch_checked(watch, event->wd);
        if (slot > -1) {
            reinitialize(watch);
        }
        // Discard all remaining events in current `read` buffer.
        evtlen = INOTIFY_READ_BUF_LEN;
    } else if (event->mask & IN_UNMOUNT) {
        // When a filesystem is unmounted, each of the watches on the is
        // dropped, and an unmount and an ignore event are generated. There's
        // nothing left for us to monitor, so we just remove the corresponding
        // cache entry.
#if DEBUG
        printf("filesystem unmounted: %s\n", path);
        fflush(stdout);
#endif
        send_watcher_kill_signal(watch->processevtfd);
        mark_cache_slot_empty(slot);
        // No need to remove the watch; that happens automatically.
    } else if (event->mask & IN_MOVE_SELF &&
        find_root_path(watch, path) != NULL) {
        // If the root path moves to a new location in the same filesystem,
        // then all cached pathnames become invalid, and we have no direct way
        // of knowing the new name of the root path. We could in theory find
        // the new name by caching the inode of the root path on start-up and
        // then trying to find a pathname that corresponds to that inode.
        // Instead, we'll keep things simple, and just cease monitoring it.
#if DEBUG
        printf("root path moved: %s\n", path);
        fflush(stdout);
#endif
        remove_root_path(watch, path);

        if (remove_subtree(watch, path) == -1) {
            // Cache reached an inconsistent state.
            slot = find_watch_checked(watch, event->wd);
            if (slot > -1) {
                reinitialize(watch);
            }
            // Discard all remaining events in current `read` buffer.
            return INOTIFY_READ_BUF_LEN;
        }
    }

    slot = find_watch_checked(watch, event->wd);
    if (slot == -1 ||
        // Only continue with the events we care about.
        !(event->mask & watch->event_mask)) {
        // Discard all remaining events in current `read` buffer.
        return sizeof(struct inotify_event) + event->len;
    }

    struct arguswatch_event awevent = {
        .watch = watch,
        .event_mask = event->mask,
        .path_name = path,                          // Name of the watched directory.
        .file_name = event->len ? event->name : "", // Name of the file.
        .is_dir = (bool)(event->mask & IN_ISDIR)
    };

#if DEBUG
    printf("send event: path = %s; file: %s; event mask = %d; dir: %d\n", awevent.path_name,
        awevent.file_name, awevent.event_mask, awevent.is_dir);
    fflush(stdout);
#endif

    // Call ArgusdImpl log function passed into this watch.
    logfn(&awevent);

    check_cache_consistency(watch);

    return evtlen;
}

/**
 * Read all available `inotify` events from the file descriptor `fd`.
 *
 * @param watch
 * @param logfn
 * @return
 */
static int process_inotify_events(struct arguswatch *watch, void (*logfn)(struct arguswatch_event *)) {
    // Some systems cannot read integer variables if they are not properly
    // aligned on other systems, incorrect alignment may decrease performance
    // hence, the buffer used for reading from the `inotify` file descriptor
    // should have the same alignment as struct inotify_event.
    char buf[INOTIFY_READ_BUF_LEN] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t len, nr;
    size_t evtlen;
    int i;
    bool first = true;
    char *ptr = NULL;
    struct sigaction sa;

    void alarm_handler(int sig) {
        // Just interrupt `read`.
        return;
    }
    // SIGALRM handler is designed simply to interrupt `read`.
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = alarm_handler;
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, NULL) == EOF) {
#if DEBUG
        perror("sigaction");
#endif
        return EOF;
    }

    len = read(watch->fd, buf, INOTIFY_READ_BUF_LEN);
    if (len == EOF) {
#if DEBUG
        perror("read");
#endif
        return EOF;
    } else if (len == 0) {
#if DEBUG
        fprintf(stderr, "`read` from `inotify` fd returned 0!");
#endif
        return EOF;
    }

    // Process each event in the buffer returned by `read` loop over all events
    // in the buffer.
    for (ptr = buf; ptr < buf + len; /*ptr += sizeof(struct inotify_event) + event->len*/) {
        evtlen = process_next_inotify_event(watch, ptr, buf + len - ptr, first, logfn);

        if (evtlen > 0) {
            ptr += evtlen;
            first = true;
        } else {
            // We got here because an IN_MOVED_FROM event was found at the end
            // of a previously read buffer and that event may be part of an
            // "intra-tree" `rename`, meaning that we should check if there is
            // a subsequent IN_MOVED_TO event with the same cookie value. We
            // left that event unprocessed and we will now try to read some
            // more events, delaying for a short time, to give the associated
            // IN_MOVED_IN event (if there is one) a chance to arrive. However,
            // we only want to do this once: if the `read` below fails to
            // gather further events, then when we reprocess the IN_MOVED_FROM
            // we should treat it as though this is an out-of-tree `rename`.
            // Set `first` to 0 for the next `process_next_inotify_event` call.
            int savederr;
            first = false;
            len = buf + len - ptr;

            // Shuffle remaining bytes to start of buffer.
            for (i = 0; i < len; ++i) {
                buf[i] = ptr[i];
            }

            // Set a timeout for `read. Some rough testing suggests that a
            // 2ms timeout is sufficient to ensure that, in around 99.8% of
            // cases, we get the IN_MOVED_TO event (if there is one) that
            // matched an IN_MOVED_FROM event, even in a highly dynamic
            // directory tree. This number may warrant tuning on different
            // hardware and in environments with different filesystem activity
            // levels.
            ualarm(2000, 0);
            nr = read(watch->fd, buf + len, INOTIFY_READ_BUF_LEN - len);

            // In case `ualarm` should change errno.
            savederr = errno;
            // Cancel alarm.
            ualarm(0, 0);
            errno = savederr;

            if (nr == EOF && errno != EINTR) {
#if DEBUG
                perror("read");
#endif
                return EOF;
            } else if (nr == 0) {
#if DEBUG
                fprintf(stderr, "`read` from `inotify` fd returned 0!");
#endif
                return EOF;
            }

            if (errno != -1) {
                len += nr;
#if DEBUG
                printf("secondary `read` got %zd bytes\n", nr);
                fflush(stdout);
#endif
            } else {
                // EINTR
#if DEBUG
                printf("secondary `read` got nothing\n");
                fflush(stdout);
#endif
            }
            // Start again at beginning of buffer.
            ptr = buf;
        }
    }
    return 0;
}

/**
 * Starts the `inotify` watcher process. Acts as the `main` function if this
 * was a standlone program. It is called from the main implementation of this
 * daemon in a new thread each time it is invoked. Once started up, it creates
 * the initial cache objects, traverses the tree of given paths, either
 * recursive or not, and loops infinitely waiting for new `inotify` events
 * until it receives a kill signal.
 *
 * @param name
 * @param pid
 * @param sid
 * @param nodename
 * @param podname
 * @param pathc
 * @param paths
 * @param ignorec
 * @param ignores
 * @param mask
 * @param onlydir
 * @param recursive
 * @param maxdepth
 * @param processevtfd
 * @param tags
 * @param logformat
 * @param logfn
 * @return
 */
int start_inotify_watcher(char *name, const int pid, const int sid, char *nodename, char *podname, unsigned int pathc, char *paths[],
    unsigned int ignorec, char *ignores[], uint32_t mask, bool onlydir, bool recursive, int maxdepth, int processevtfd,
    char *tags, char *logformat, void (*logfn)(struct arguswatch_event *)) {

    int fd, pollc;
    nfds_t nfds;
    struct pollfd fds[2];
    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGCHLD);

    // To keep this function idempotent we need to handle both existing
    // arguswatch configuration updates as well as new ones. `inotify_add_watch`
    // will also handle updates properly if a wd exists for the supplied path.
    struct arguswatch watch;
    int slot = find_cached_slot(pid, sid);
    if (slot > -1) {
        watch = wlcache[slot];
    } else {
        // Create new arguswatch placeholder struct, to be filled later.
        watch = (struct arguswatch){
            .name = name,
            .pid = pid,
            .sid = sid,
            .node_name = nodename,
            .pod_name = podname,
            .slot = -1,
            .fd = EOF,
            .rootpathc = pathc,
            .rootpaths = paths,
            .pathc = 0,
            .ignorec = ignorec,
            .ignores = ignores,
            .event_mask = mask,
            .only_dir = onlydir,
            .recursive = recursive,
            .max_depth = maxdepth,
            .processevtfd = processevtfd,
            .tags = tags,
            .log_format = logformat
        };
    }

    // Save a copy of the paths.
    copy_root_paths(&watch);

#if DEBUG
    printf("  Listening for events (pid = %d, sid = %d)\n", pid, sid);
    fflush(stdout);
#endif

    // Create an `inotify` instance and populate it with entries for paths.
    fd = reinitialize(&watch);
    if (fd == EOF) {
        goto exit;
    }

    // Prepare for polling.
    nfds = 2;
    // `inotify` input.
    fds[0].fd = fd;
    fds[0].events = POLLIN;
    // Anonymous pipe for manual kill.
    fds[1].fd = processevtfd;
    fds[1].events = POLLIN;

    // Wait for events.
    for (;;) {
        pollc = ppoll(fds, nfds, NULL, &sigmask);
        if (pollc == EOF) {
            if (errno == EINTR) {
                continue;
            }
#if DEBUG
            perror("ppoll");
#endif
            goto exit;
        }

        if (pollc > 0) {
            if (fds[0].revents & POLLIN) {
                // `inotify` events are available.
                if (process_inotify_events(&watch, logfn) == EOF) {
                    //goto exit;
                }
            }

            if (fds[1].revents & POLLIN) {
                // Anonymous pipe events are available.
                uint64_t value;
                ssize_t len = read(fds[1].fd, &value, sizeof(uint64_t));
                if (len != EOF &&
                    (value & ARGUSNOTIFY_KILL)) {
                    break;
                }
            }
        }
    }

exit:
#if DEBUG
    printf("  Listening for events stopped (pid = %d, sid = %d)\n", pid, sid);
    fflush(stdout);
#endif

    // Close `inotify` file descriptor.
    close(fd);
    // Free watch cache.
    free_cache(&watch);

    return errno ? EXIT_FAILURE : EXIT_SUCCESS;
}

/**
 * Sends the custom kill signal to break out of the `ppoll` loop that is
 * listening for active `inotify` watch events.
 *
 * @param processfd
 */
void send_watcher_kill_signal(int processfd) {
    uint64_t value = ARGUSNOTIFY_KILL;
    if (write(processfd, &value, sizeof(value)) == EOF) {
#if DEBUG
        perror("write");
#endif
    }
}
