#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
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

#include "fimnotify.h"

/**
 * read all available inotify events from the file descriptor `fd`
 * `wd` is the table of watch descriptors for the directories in `paths`
 * `pathc` is the length of `wd` and `paths`
 * `paths` [0->N-1] is the list of watched directories
 */
static void handle_events(int fd, int *wd, int pathc, char *paths[], mqd_t mq) {
    /**
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
        if (len == EOF && errno != EAGAIN) {
#if DEBUG
            perror("read");
#endif
            exit(EXIT_FAILURE);
        }

        // if the non-blocking `read()` found no events to read, then it
        // returns with -1 with `errno` set to `EAGAIN`; exit the loop
        if (len == EOF) {
            break;
        }

        // loop over all events in the buffer
        for (ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
            event = (const struct inotify_event *)ptr;

            // reset message each time
            struct fimwatch_event fwevent;
            fwevent.event_mask = event->mask;
            // name of the watched directory
            for (i = 0; i < pathc; i++) {
                if (wd[i] == event->wd) {
                    fwevent.path_name = paths[i];
                    break;
                }
            }
            // name of the file
            if (event->len) {
                fwevent.file_name = event->name;
            }
            fwevent.is_dir = event->mask & IN_ISDIR;

#if DEBUG
            printf("[%d]\t%s/%s\t[%d]\n", fwevent.event_mask, fwevent.path_name, fwevent.file_name, fwevent.is_dir);
            fflush(stdout);
#endif

            // @TODO: document this
            if (mq_send(mq, (const char *)&fwevent, sizeof(fwevent), 0) == EOF) {
#if DEBUG
                perror("mq_send");
#endif
            }
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
#if DEBUG
        perror("open");
#endif
        exit(EXIT_FAILURE);
    }

    // join namespace
    if (setns(fd, CLONE_NEWNS) == EOF) {
#if DEBUG
        perror("setns");
#endif
        exit(EXIT_FAILURE);
    }

#if DEBUG
    printf("Joined namespace: %s.\n", file);
    fflush(stdout);
#endif

    // close namespace file descriptor
    close(fd);
}

int start_inotify_watcher(int pathc, char *paths[], uint32_t event_mask, int process_eventfd) {
    int fd, i, poll_num;
    int *wd;
    nfds_t nfds;
    struct pollfd fds[2];
    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGCHLD);
    mqd_t mq;

    // create the file descriptor for accessing the inotify API
    fd = inotify_init1(IN_NONBLOCK);
    if (fd == EOF) {
#if DEBUG
        perror("inotify_init1");
#endif
        goto exit;
    }

    // allocate memory for watch descriptors
    wd = calloc(pathc, sizeof(int));
    if (wd == NULL) {
#if DEBUG
        perror("calloc");
#endif
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

#if DEBUG
    printf("  Listening for events on:\n");
#endif

    bool do_polling;
    // make directories for events
    for (i = 0; i < pathc; ++i) {
        wd[i] = inotify_add_watch(fd, paths[i], event_mask);
        if (wd[i] == EOF) {
#if DEBUG
            fprintf(stderr, "Cannot watch '%s' (%d)\n", paths[i], errno);
            perror("inotify_add_watch");
            printf("    [ ] %s\n", paths[i]);
#endif
        } else {
#if DEBUG
            printf("    [*] %s\n", paths[i]);
#endif
            do_polling = true;
        }
    }
    fflush(stdout);

    if (!do_polling) {
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
                handle_events(fd, wd, pathc, paths, mq);
            }

            if (fds[1].revents & POLLIN) {
                // anonymous pipe events are available
                uint64_t value;
                int len = read(fds[1].fd, &value, sizeof(uint64_t));
                if (len != EOF && value & FIMNOTIFY_KILL) {
                    break;
                }
            }
        }
    }

#if DEBUG
    printf("  Listening for events stopped.\n");
    fflush(stdout);
#endif

    for (i = 0; i < pathc; ++i) {
        int ret = inotify_rm_watch(fd, wd[i]);
#if DEBUG
        if (ret == EOF) {
            fprintf(stderr, "Cannot remove '%s' (%d)\n", paths[i], errno);
            perror("inotify_rm_watch");
        }
#endif
    }

exit:
    // close inotify file descriptor
    if (fd != EOF) {
        close(fd);
    }
    // deallocate memory for watch descriptors
    if (wd != NULL) {
        free(wd);
    }
    // close message queue
    if (mq != EOF) {
        mq_close(mq);
    }

    return errno ? EXIT_FAILURE : EXIT_SUCCESS;
}
