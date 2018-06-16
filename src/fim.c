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

static void handle_events(int fd, int *wd, int argc, char *argv[]) {
  char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
  const struct inotify_event *event;
  int i;
  ssize_t len;
  char *ptr;

  for (;;) {
    len = read(fd, buf, sizeof(buf));
    if (len == -1 && errno != EAGAIN) {
      errExit("read");
    }

    if (len <= 0) {
      break;
    }

    for (ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
      event = (const struct inotify_event *)ptr;
      
      if (event->mask & IN_OPEN) {
        printf("IN_OPEN: ");
      }
      if (event->mask & IN_MODIFY) {
        printf("IN_MODIFY: ");
      }

      for (i = 1; i < argc; ++i) {
        if (wd[i] == event->wd) {
          printf("%s/", argv[i]);
          break;
        }
      }

      if (event->len) {
        printf("%s", event->name);
      }

      if (event->mask & IN_ISDIR) {
        printf(" [directory]\n");
      } else {
        printf(" [file]\n");
      }
    }
  }
}

int main(int argc, char *argv[]) {
  char buf;
  int fd, i, poll_num;
  int *wd;
  nfds_t nfds;
  struct pollfd fds[2];

  if (argc < 3) {
    fprintf(stderr, "%s </proc/PID/ns/FILE> <paths...>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  printf("Press ENTER key to terminate.\n");

  // get file descriptor for namespace
  fd = open(argv[1], O_RDONLY);
  if (fd == -1) {
    errExit("open");
  }

  // join namespace
  if (setns(fd, 0) == -1) {
    errExit("setns");
  }

  // execute a command in namespace
  //execvp(argv[2], &argv[2]);
  //errExit("execvp");
    
  fd = inotify_init1(IN_NONBLOCK);
  if (fd == -1) {
    errExit("inotify_init1");
  }
 
  wd = calloc(argc, sizeof(int));
  if (wd == NULL) {
    errExit("calloc");
  }
  
  for (i = 1; i < argc; i++) {
    wd[i] = inotify_add_watch(fd, argv[2], IN_OPEN | IN_MODIFY);
    if (wd[i] == -1) {
      fprintf(stderr, "Cannot watch '%s'\n", argv[i]);
      errExit("inotify_add_watch");
    }
  }

  nfds = 2;

  fds[0].fd = STDIN_FILENO;
  fds[0].events = POLLIN;

  fds[1].fd = fd;
  fds[1].events = POLLIN;

  printf("Listening for events.\n");
  while (1) {
    poll_num = poll(fds, nfds, -1);
    if (poll_num == -1) {
      if (errno == EINTR) {
        continue;
      }
      errExit("poll");
    }

    if (poll_num > 0) {
      if (fds[0].revents & POLLIN) {
        while (read(STDIN_FILENO, &buf, 1) > 0 && buf != '\n') {
          continue;
        }
        break;
      }

      if (fds[1].revents & POLLIN) {
        handle_events(fd, wd, argc, argv);
      }
    }
  }

  printf("Listening for events stopped.\n");

  close(fd);
  free(wd);
  exit(EXIT_SUCCESS);
}
