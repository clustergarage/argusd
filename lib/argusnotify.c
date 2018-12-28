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
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
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
static void reinitialize(struct arguswatch **/*restrict */watch) {
    int fd, slot;
    bool rebuild = (*watch)->fd != EOF;

    if (rebuild) {
        close((*watch)->fd);
    } else {
#if DEBUG
        printf("initializing cache\n");
        fflush(stdout);
#endif
    }

    fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    printf(" !!! %s :: fd => %d\n", __func__, fd);
    fflush(stdout);
    if (fd == EOF) {
#if DEBUG
        perror("inotify_init1");
#endif
        return;
    }
    (*watch)->fd = fd;
#if DEBUG
    printf("  new fd = %d\n", fd);
    fflush(stdout);
#endif

    // Free watch cache.
    clear_watch(watch);
    // Begin traversing tree, or non-recursive directories.
    watch_subtree(watch);

    (*watch)->processevtfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE);
    printf(" !!! %s :: processfd => %d\n", __func__, (*watch)->processevtfd);
    fflush(stdout);
    if ((*watch)->processevtfd == EOF) {
#if DEBUG
        perror("eventfd");
#endif
    }

    slot = find_cached_slot((*watch)->pid, (*watch)->sid);
    if (slot == -1) {
        // Cache information about the watch.
        add_watch_to_cache(watch);
    }

#if DEBUG
    if (rebuild) {
        printf("rebuilt watch with %d entries\n", (*watch)->pathc);
        fflush(stdout);
    }
#endif

    // Check cache consistency right away, in case there are multiple
    // containers in a single pod that don't have a path on the filesystem that
    // we specified to watch.
    check_cache_consistency(watch);
}

/**
 * Process the next `inotify` event in the buffer specified by `event` and
 * `len`. In most cases, a single event is consumed, but if there is an * IN_MOVED_FROM+IN_MOVED_TO pair that share a cookie value, both events are
 * consumed returns the number of bytes in the event(s) consumed from `event`.
 *
 * @param watch
 * @param event
 * @param len
 * @param first
 * @param logfn
 * @return
 */
static size_t process_next_inotify_event(struct arguswatch **/*restrict*/ watch, const struct inotify_event *event,
    const ssize_t len, const bool first, arguswatch_logfn logfn) {

    const char *path = NULL;
    char fullpath[PATH_MAX + NAME_MAX + 1];
    int slot, wdslot;
    size_t evtlen;

    if (event->wd != EOF) {
        slot = find_watch_checked(*watch, event->wd);
        if (slot == -1 ||
            // Only continue with the events we care about.
            !(event->mask & (*watch)->event_mask)) {
            // Discard all remaining events in current `read` buffer.
            return IN_BUFFER_SIZE;
        }

        path = wd_to_path_name(*watch, event->wd);

        struct arguswatch_event awevent = {
            .watch = *watch,
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
        (*logfn)(&awevent);

        if (!(event->mask & IN_IGNORED)) {
            // IN_Q_OVERFLOW has (event->wd == EOF). Skip IN_IGNORED, since it
            // will come after an event that has already removed the
            // corresponding cache entry. Cache consistency check. See the
            // discussion of "intra-tree" `rename` events.
            slot = find_watch_checked(*watch, event->wd);
            if (slot == -1) {
                // Reinitialize the `inotify` watch.
                (*watch)->fd = EOF;
                // Cache reached an inconsistent state.
                reinitialize(watch);
                // Discard all remaining events in current `read` buffer.
                return IN_BUFFER_SIZE;
            }
        }
    }

    evtlen = sizeof(struct inotify_event) + event->len;

    if ((event->mask & IN_ISDIR) &&
        (event->mask & (IN_CREATE | IN_MOVED_TO))) {
        // A new subdirectory was created, or a subdirectory was renamed into
        // the tree. Create watches for it, and all of its subdirectories.
        FORMAT_PATH(fullpath, path, event->name);

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
        if (path_name_to_cache_slot(*watch, fullpath) == -1) {
            wdslot = find_watch(*watch, event->wd);
            if (wdslot > -1 &&
                // Only do this if watching recursively.
                (*watch)->recursive) {
                (*watch)->pathc = 0;
                watch_subtree(watch);
                // @TODO: Verify that this works.
                wlcache[(*watch)->slot] = *watch;
            }
        }
    } else if (event->mask & IN_DELETE_SELF) {
        // A directory was deleted. Remove the corresponding item from the
        // cache.
#if DEBUG
        printf("clearing watchlist item %d (%s)\n", event->wd, path);
        fflush(stdout);
#endif
        if (find_root_path(*watch, path) != NULL) {
            remove_root_path(watch, path);
        }
        check_cache_consistency(watch);
        // ... no need to remove the watch, that happens automatically.
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
        const struct inotify_event *nextevent = event + evtlen;

        if ((nextevent < event + len) &&
            (nextevent->mask & IN_MOVED_TO) &&
            (nextevent->cookie == event->cookie)) {

            // We have a `rename` event. We need to fix up the cached pathnames
            // for the corresponding directory and all of its subdirectories.
            int nextslot = find_watch_checked(*watch, nextevent->wd);
            if (nextslot == -1) {
                // Reinitialize the `inotify` watch.
                (*watch)->fd = EOF;
                // Cache reached an inconsistent state.
                reinitialize(watch);
                // Discard all remaining events in current `read` buffer.
                return IN_BUFFER_SIZE;
            }

            rewrite_cached_paths(watch, path, event->name,
                wd_to_path_name(*watch, nextevent->wd), nextevent->name);

            // Also processed the next (IN_MOVED_TO) event, so skip over it.
            evtlen += sizeof(struct inotify_event) + nextevent->len;
        } else if ((nextevent < event + len) || !first) {
            // Got a "moved from" event without an accompanying "moved to"
            // event. The directory has been moved outside the tree we are
            // monitoring need to remove the watches and remove the cache
            // entries for the moved directory and all of its subdirectories.
#if DEBUG
            printf("moved out: %p %p\n", (void *)path, (void *)event->name);
            printf("first = %d; remaining bytes = %ld\n", first, event + len - nextevent);
            fflush(stdout);
#endif
            FORMAT_PATH(fullpath, path, event->name);

            slot = find_watch_checked(*watch, event->wd);
            if (slot > -1 &&
                remove_subtree(watch, fullpath) == -1) {
                // Cache reached an inconsistent state.
                reinitialize(watch);
                // Discard all remaining events in current `read` buffer.
                return IN_BUFFER_SIZE;
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
        slot = find_watch_checked(*watch, event->wd);
        if (slot > -1) {
            reinitialize(watch);
        }
        // Discard all remaining events in current `read` buffer.
        evtlen = IN_EVENT_LEN;
    } else if (event->mask & IN_UNMOUNT) {
        // When a filesystem is unmounted, each of the watches on the is
        // dropped, and an unmount and an ignore event are generated. There's
        // nothing left for us to monitor, so we just remove the corresponding
        // cache entry.
#if DEBUG
        printf("filesystem unmounted: %s\n", path);
        fflush(stdout);
#endif
        send_watcher_kill_signal((*watch)->pid);
        mark_cache_slot_empty((*watch)->slot);
        // No need to remove the watch; that happens automatically.
    } else if (event->mask & IN_MOVE_SELF &&
        find_root_path(*watch, path) != NULL) {

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

        if ((*watch)->follow_move) {
            find_replace_root_path(watch, path);
            reinitialize(watch);
        } else {
            remove_root_path(watch, path);
            if (remove_subtree(watch, path) == -1) {
                // Cache reached an inconsistent state.
                slot = find_watch_checked(*watch, event->wd);
                if (slot > -1) {
                    reinitialize(watch);
                }
                // Discard all remaining events in current `read` buffer.
                return IN_BUFFER_SIZE;
            }
        }
    }

    //check_cache_consistency(watch);

    return evtlen;
}

/**
 * Read all available `inotify` events from the file descriptor `fd`.
 *
 * @param watch
 * @param logfn
 * @return
 */
static void process_inotify_events(struct arguswatch **watch, arguswatch_logfn logfn) {
    const struct inotify_event *event;
    // Some systems cannot read integer variables if they are not properly
    // aligned on other systems, incorrect alignment may decrease performance
    // hence, the buffer used for reading from the `inotify` file descriptor
    // should have the same alignment as struct inotify_event.
    struct inotify_event buf[IN_BUFFER_SIZE] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t len, readlen;
    size_t evtlen;
    int i;
    bool first = true;
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    void alarm_handler(int sig) {
        // Just interrupt `read`.
        return;
    }
    // SIGALRM handler is designed simply to interrupt `read`.
    sa.sa_handler = alarm_handler;
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, NULL) == EOF) {
#if DEBUG
        perror("sigaction");
#endif
        return;
    }

    len = read((*watch)->fd, (void *)&buf, IN_BUFFER_SIZE);
    if (len == EOF) {
        if (errno != EAGAIN) {
#if DEBUG
            perror("read");
#endif
        }
        return;
    } else if (len == 0) {
#if DEBUG
        fprintf(stderr, "`read` from `inotify` fd returned 0!");
#endif
        return;
    }
#if DEBUG
    printf("`read` got %zd bytes\n", len);
    fflush(stdout);
#endif

    // Point to the first event in the buffer.
    event = buf;

    // Process each event in the buffer returned by `read`. Loop over all
    // events in the buffer.
    while (IN_EVENT_OK(event, buf, len)) {
        evtlen = process_next_inotify_event(watch, event, buf + len - event, first, logfn);
        // Set `first` for the next `process_next_inotify_event` call.
        first = (bool)(evtlen > 0);

        if (evtlen == EOF) {
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
            int savederr;
            len = buf + len - event;

            // Shuffle remaining bytes to start of buffer.
            for (i = 0; i < len; ++i) {
                buf[i] = event[i];
            }

            // Set a timeout for `read`. Some rough testing suggests that a
            // 2ms timeout is sufficient to ensure that, in around 99.8% of
            // cases, we get the IN_MOVED_TO event (if there is one) that
            // matched an IN_MOVED_FROM event, even in a highly dynamic
            // directory tree. This number may warrant tuning on different
            // hardware and in environments with different filesystem activity
            // levels.
            ualarm(2000, 0);
            readlen = read((*watch)->fd, buf + len, IN_BUFFER_SIZE);

            // In case `ualarm` should change errno.
            savederr = errno;
            // Cancel alarm.
            ualarm(0, 0);
            errno = savederr;

            if (readlen == EOF && errno != EINTR) {
#if DEBUG
                perror("read");
#endif
                return;
            } else if (readlen == 0) {
#if DEBUG
                fprintf(stderr, "`read` from `inotify` fd returned 0!");
#endif
                return;
            }

            if (errno != -1) {
                len += readlen;
#if DEBUG
                printf("secondary `read` got %zd bytes\n", readlen);
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
            event = buf;
        }

        // Advance to next event.
        event = IN_EVENT_NEXT(event, len, evtlen);
    }

#if 0
    printf(" === %s => DUMP_CACHE \n", __func__);
    DUMP_CACHE(*watch);
#endif
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
 * @param followmove
 * @param tags
 * @param logformat
 * @param logfn
 * @return
 */
int start_inotify_watcher(const char *name, const char *nodename, const char *podname, const int pid, const int sid,
    const unsigned int pathc, const char *paths[], const unsigned int ignorec, const char *ignores[], const uint32_t mask,
    const bool onlydir, const bool recursive, const int maxdepth, const bool followmove, const char *tags,
    const char *logformat, arguswatch_logfn logfn) {

    struct arguswatch *watch;
    // To keep this function idempotent we need to handle both existing
    // arguswatch configuration updates as well as new ones.
    // `inotify_add_watch` will also handle updates properly if a wd exists for
    // the supplied path.
    int slot = find_cached_slot(pid, sid);
    if (slot > -1 &&
        wlcache[slot] != NULL) {
        watch = &*wlcache[slot];
    } else {
        // Create new arguswatch placeholder struct with select watch
        // parameters that cannot change; the rest to be filled later.
        watch = &(struct arguswatch){
            .name = name,
            .node_name = nodename,
            .pod_name = podname,
            .pathc = 0,
            .pid = pid,
            .sid = sid,
            .slot = -1,
            .fd = EOF
        };
    }

    // Assign or update the passed-in watch parameters that can possibly change
    // between recreation of an existing watcher.
    watch->rootpathc = pathc;
    watch->rootpaths = (char **)paths;
    watch->ignorec = ignorec;
    watch->ignores = (char **)ignores;
    watch->event_mask = mask;
    watch->only_dir = onlydir;
    watch->recursive = recursive;
    watch->max_depth = maxdepth;
    watch->follow_move = followmove;
    watch->tags = tags;
    watch->log_format = logformat;

    // Validate root paths with `stat` and for duplicates.
    validate_root_paths(watch);

#if DEBUG
    printf("  Listening for events (pid = %d, sid = %d)\n", pid, sid);
    fflush(stdout);
#endif

    // Create an `inotify` instance and populate it with entries for paths.
    reinitialize(&watch);
    if (watch->fd == EOF) {
#if DEBUG
        perror("reinitialize");
#endif
        goto out;
    }

    struct epoll_event epollevt[2];
    // Buffer where events are returned.
    struct epoll_event *epollevts = calloc(EPOLL_MAX_EVENTS, sizeof(struct epoll_event));
    uint32_t eflags = EPOLLIN | EPOLLET | EPOLLERR | EPOLLHUP;
    int efd, nfds, i;
    sigset_t sigmask, origmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGCHLD);
    sigaddset(&sigmask, SIGHUP);
    pthread_sigmask(SIG_SETMASK, &sigmask, &origmask);

    efd = epoll_create1(EPOLL_CLOEXEC);
    printf(" !!! %s :: efd => %d\n", __func__, efd);
    fflush(stdout);
    if (efd == EOF) {
#if DEBUG
        perror("epoll_create");
#endif
        goto out;
    }

    // `inotify` input.
    epollevt[0].data.fd = watch->fd;
    epollevt[0].events = eflags;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, watch->fd, &epollevt[0]) == EOF) {
#if DEBUG
        perror("epoll_ctl");
#endif
    }
    // Anonymous pipe for manual kill.
    epollevt[1].data.fd = watch->processevtfd;
    epollevt[1].events = eflags;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, watch->processevtfd, &epollevt[1]) == EOF) {
#if DEBUG
        perror("epoll_ctl");
#endif
    }

    printf(" ### fd = %d; processeventfd = %d; epollfd = %d\n", watch->fd, watch->processevtfd, efd);
    fflush(stdout);

    // Wait for events.
    for (;;) {
        //nfds = epoll_wait(efd, epollevts, EPOLL_MAX_EVENTS, -1);
        nfds = epoll_pwait(efd, epollevts, EPOLL_MAX_EVENTS, -1, &sigmask);
        pthread_sigmask(SIG_SETMASK, &origmask, NULL);
        if (nfds == EOF) {
            if (errno == EINTR) {
                continue;
            }
#if DEBUG
            perror("epoll_pwait");
#endif
            goto out;
        }

        for (i = 0; i < nfds; ++i) {
            if ((epollevts[i].events & EPOLLERR) ||
                (epollevts[i].events & EPOLLHUP) ||
                (!(epollevts[i].events & EPOLLIN))) {
#if DEBUG
                fprintf(stderr, "epoll error\n");
#endif
                close(epollevts[i].data.fd);
                continue;
            }

            if (epollevts[i].data.fd == watch->fd) {
                // `inotify` events are available.
                process_inotify_events(&watch, logfn);
            } else if (epollevts[i].data.fd == watch->processevtfd) {
                // Anonymous pipe events are available.
                uint64_t value;
                ssize_t len = read(epollevts[i].data.fd, &value, sizeof(uint64_t));
                printf(" ### processevtfd = %d (value & ARGUSNOTIFY_KILL) = %d ### \n", watch->processevtfd, (bool)(value & ARGUSNOTIFY_KILL));
                if (len != EOF &&
                    (value & ARGUSNOTIFY_KILL)) {
                    goto out;
                }
            }
        }
    }

out:
#if DEBUG
    printf("  Listening for events stopped (pid = %d, sid = %d)\n", pid, sid);
    fflush(stdout);
#endif

    if (epoll_ctl(efd, EPOLL_CTL_DEL, watch->fd, NULL) == EOF) {
#if DEBUG
        perror("epoll_ctl");
#endif
    }
    if (epoll_ctl(efd, EPOLL_CTL_DEL, watch->processevtfd, NULL) == EOF) {
#if DEBUG
        perror("epoll_ctl");
#endif
    }

    // Close `inotify` file descriptor.
    if (close(watch->fd) == EOF) {
#if DEBUG
        perror("close");
#endif
    }
    // Close `eventfd` file descriptor.
    if (close(watch->processevtfd) == EOF) {
#if DEBUG
        perror("close");
#endif
    }
    // Close `epoll` file descriptor.
    if (close(efd) == EOF) {
#if DEBUG
        perror("close");
#endif
    }
    // Free epoll event memory.
    free(epollevts);

    // Free watch cache.
    clear_watch(&watch);

    return errno ? EXIT_FAILURE : EXIT_SUCCESS;
}

/**
 * Sends the custom kill signal to break out of the `epoll` loop that is
 * listening for active `inotify` watch events.
 *
 * @param pid
 */
void send_watcher_kill_signal(const int pid) {
    int i;
    for (i = 0; i < wlcachec; ++i) {
        if (wlcache[i]->pid == pid) {
            uint64_t value = ARGUSNOTIFY_KILL;
            if (write(wlcache[i]->processevtfd, &value, sizeof(value)) == EOF) {
#if DEBUG
                perror("write");
#endif
            }
        }
    }
}
