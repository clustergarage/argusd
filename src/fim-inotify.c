#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <unistd.h>

#define errExit(msg) do { \
  perror(msg); \
  exit(EXIT_FAILURE); \
} while (0)

/**
 * read all available inotify events from the file descriptor `fd`
 * `wd` is the table of watch descriptors for the directories in `argv`
 * `argc` is the length of `wd` and `argv`
 * `argv` [1-N] is the list of watched directories
 */
static void handle_events(int fd, int *wd, int argc, char *argv[]) {
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
    if (len == -1 && errno != EAGAIN) {
      errExit("read");
    }

    // if the non-blocking `read()` found no events to read, then it
    // returns with -1 with `errno` set to `EAGAIN`; exit the loop
    if (len <= 0) {
      break;
    }

    // loop over all events in the buffer
    for (ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
      event = (const struct inotify_event *)ptr;

      // print event type
      if (event->mask & IN_OPEN) {
        fprintf(stdout, "IN_OPEN: ");
      }
      if (event->mask & IN_MODIFY) {
        fprintf(stdout, "IN_MODIFY: ");
      }

      // print the name of the watched directory
      for (i = 1; i < argc; ++i) {
        if (wd[i] == event->wd) {
          fprintf(stdout, "%s/", argv[i]);
          break;
        }
      }

      // print the name of the file
      if (event->len) {
        fprintf(stdout, "%s", event->name);
      }

      // print the type of filesystem object
      if (event->mask & IN_ISDIR) {
        fprintf(stdout, " [directory]\n");
      } else {
        fprintf(stdout, " [file]\n");
      }
      fflush(stdout);
    }
  }
}

int main(int argc, char *argv[]) {
  char buf;
  int fdns, fdin, i, poll_num;
  int *wd;
  nfds_t nfds;
  struct pollfd fds[1];

  if (argc < 3) {
    fprintf(stderr, "%s </proc/PID/ns/NAMESPACE> <paths...>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  // -- JOIN THE NAMESPACE

  // get file descriptor for namespace
  fdns = open(argv[1], O_RDONLY);
  if (fdns == -1) {
    errExit("open");
  }

  // join namespace
  if (setns(fdns, 0) == -1) {
    errExit("setns");
  }

  // close namespace file descriptor
  close(fdns);

  // -- START THE INOTIFY WATCHER

  //printf("Press ENTER key to terminate.\n");

  // create the file descriptor for accessing the inotify API
  fdin = inotify_init1(IN_NONBLOCK);
  if (fdin == -1) {
    errExit("inotify_init1");
  }

  // allocate memory for watch descriptors
  wd = calloc(argc, sizeof(int));
  if (wd == NULL) {
    errExit("calloc");
  }

  /**
   * make directories for events
   * - file was opened
   * - file was modified
   */
  for (i = 1; i < argc; i++) {
    wd[i] = inotify_add_watch(fdin, argv[2], IN_OPEN | IN_MODIFY);
    if (wd[i] == -1) {
      fprintf(stderr, "Cannot watch '%s'\n", argv[i]);
      errExit("inotify_add_watch");
    }
  }

  // prepare for polling
  nfds = 2;
  // console input
  //fds[0].fd = STDIN_FILENO;
  //fds[0].events = POLLIN;
  // inotify input
  fds[0].fd = fdin;
  fds[0].events = POLLIN;

  // wait for events and/or terminal input
  fprintf(stdout, "Listening for events.\n");
  fflush(stdout);

  while (1) {
    poll_num = poll(fds, nfds, -1);
    if (poll_num == -1) {
      if (errno == EINTR) {
        continue;
      }
      errExit("poll");
    }

    if (poll_num > 0) {
      /*
      if (fds[0].revents & POLLIN) {
        // console input is available; empty stdin and quit
        while (read(STDIN_FILENO, &buf, 1) > 0 && buf != '\n') {
          continue;
        }
        break;
      }
      */

      if (fds[0].revents & POLLIN) {
        // inotify events are available
        handle_events(fdin, wd, argc, argv);
      }
    }
  }

  fprintf(stdout, "Listening for events stopped.\n");
  fflush(stdout);

  // close inotify file descriptor
  close(fdin);
  free(wd);

  exit(EXIT_SUCCESS);
}
