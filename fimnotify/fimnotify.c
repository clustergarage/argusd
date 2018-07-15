#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>

#include "fimnotify.h"

/**
 * read all available inotify events from the file descriptor `fd`
 * `wd` is the table of watch descriptors for the directories in `paths`
 * `pathc` is the length of `wd` and `paths`
 * `paths` [0->N-1] is the list of watched directories
 */ static void handle_events(int fd, int *wd, int pathc, char *paths[]) { /**
     * some systems cannot read integer variables if they are not properly aligned
     * on other systems, incorrect alignment may decrease performance
     * hence, the buffer used for reading from the inotify file descriptor should
     * have the same alignment as struct inotify_event
     */
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    const struct inotify_event *event;
    int i;
    ssize_t len;
    char *ptr;

    // loop while events can be read from the inotify file descriptor
    for (;;) {
        // read some events
        len = read(fd, buf, sizeof(buf));
        if (len == -1 && errno != EAGAIN) {
            errexit("read");
        }

        // if the non-blocking `read()` found no events to read, then it
        // returns with -1 with `errno` set to `EAGAIN`; exit the loop
        if (len == EOF) {
            break;
        }

        // loop over all events in the buffer
        for (ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
            event = (const struct inotify_event *)ptr;

            // print event type
            if (event->mask & IN_ACCESS) printf("IN_ACCESS: ");
            else if (event->mask & IN_MODIFY) printf("IN_MODIFY: ");
            else if (event->mask & IN_ATTRIB) printf("IN_ATTRIB: ");
            else if (event->mask & IN_OPEN) printf("IN_OPEN: ");
            else if (event->mask & IN_CLOSE_WRITE) printf("IN_CLOSE_WRITE: ");
            else if (event->mask & IN_CLOSE_NOWRITE) printf("IN_CLOSE_NOWRITE: ");
            else if (event->mask & IN_CREATE) printf("IN_CREATE: ");
            else if (event->mask & IN_DELETE) printf("IN_DELETE: ");
            else if (event->mask & IN_DELETE_SELF) printf("IN_DELETE_SELF: ");
            else if (event->mask & IN_MOVED_FROM) printf("IN_MOVED_FROM: ");
            else if (event->mask & IN_MOVED_TO) printf("IN_MOVED_TO: ");
            else if (event->mask & IN_MOVE_SELF) printf("IN_MOVE_SELF: ");
            // IN_IGNORED called when oneshot is active
            else break;

            // print the name of the watched directory
            for (i = 0; i < pathc; i++) {
                if (wd[i] == event->wd) {
                    printf("%s", paths[i]);
                    break;
                }
            }

            // print the name of the file
            if (event->len) {
                printf("/%s", event->name);
            }

            // @TODO: make file|directory watch configurable
            // print the type of filesystem object
            printf(" [%s]\n", (event->mask & IN_ISDIR ? "directory" : "file"));

            fflush(stdout);
        }
    }
}

void join_namespace(const pid_t pid, const char *ns) {
    char file[1024];
    int fd;

    // get file descriptor for namespace
    sprintf(file, "/proc/%d/ns/%s", pid, ns);
    fd = open(file, O_RDONLY);
    if (fd == EOF) {
        errexit("open");
    }

    // join namespace
    if (setns(fd, CLONE_NEWNS) == EOF) {
        errexit("setns");
    }

    printf("Joined namespace: %s.\n", file);
    fflush(stdout);

    // close namespace file descriptor
    close(fd);
}

void start_inotify_watcher(int pathc, char *paths[], uint32_t event_mask, int process_eventfd) {
    int fd, i, poll_num;
    int *wd;
    nfds_t nfds;
    struct pollfd fds[2];
    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGCHLD);

    // create the file descriptor for accessing the inotify API
    fd = inotify_init1(IN_NONBLOCK);
    if (fd == EOF) {
        errexit("inotify_init1");
    }

    // allocate memory for watch descriptors
    wd = calloc(pathc, sizeof(int));
    if (wd == NULL) {
        errexit("calloc");
    }

    printf("Listening for events on:\n");

    // make directories for events
    for (i = 0; i < pathc; ++i) {
        wd[i] = inotify_add_watch(fd, paths[i], event_mask);
        if (wd[i] == EOF) {
            fprintf(stderr, "Cannot watch '%s'\n", paths[i]);
            errexit("inotify_add_watch");
        }

        printf(" - %s\n", paths[i]);
    }
    fflush(stdout);

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
            errexit("poll");
        }

        if (poll_num > 0) {
            if (fds[0].revents & POLLIN) {
                // inotify events are available
                handle_events(fd, wd, pathc, paths);
            }

            if (fds[1].revents & POLLIN) {
                uint64_t value;
                read(fds[1].fd, &value, sizeof(value));
                if (value & FIMNOTIFY_KILL) {
                    for (i = 0; i < pathc; ++i) {
                        int ret = inotify_rm_watch(fd, wd[i]);
                        if (ret == EOF) {
                            fprintf(stderr, "Cannot remove '%s'\n", paths[i]);
                            errexit("inotify_rm_watch");
                        }
                    }
                    break;
                }
            }
        }
    }

    printf("Listening for events stopped.\n");
    fflush(stdout);

    for (i = 0; i < pathc; ++i) {
        inotify_rm_watch(fd, wd[i]);
    }

    // close inotify file descriptor
    close(fd);
    free(wd);
}

