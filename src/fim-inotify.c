#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "common.h"

static pid_t target_pid = -1;
static char *target_ns = NULL;
static char *target_paths[32] = {NULL};
static unsigned int target_pathc = 0;
static unsigned int target_events, opt_flags;

static void __attribute__((__noreturn__)) usage(void) {
	FILE *out = stdout;

	fputs(USAGE_HEADER, out);
	fprintf(out, " %s -p<pid> -n<namespace> -t<path>... [-e<event>...] [-f<format>]\n", program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs("Watch for namespace events within paths of a target PID.\n", out);

	fputs(USAGE_OPTIONS, out);
	fputs(" -p, --pid <pid>        target PID to watch\n", out);
	fputs(" -n, --ns <namespace>   target namespace {ipc|net|mnt|pid|user|uts}\n", out);
	fputs(" -t, --path <path>      target watch path(s)\n", out);
	fputs(" -e, --event <event>    event to watch {access|modify|attrib|open|close|create|delete|move|all}\n", out);
	fputs("     --only-dir         only watch path if it is a directory\n", out);
	fputs("     --dont-follow      do not follow a symlink\n", out);
	fputs("     --exclude-unlink   exclude events on unlinked objects\n", out);
	fputs("     --oneshot          only send event once\n", out);
	fputs(" -f, --format <format>  custom log format\n", out);

	fputs(USAGE_SEPARATOR, out);
	printf(USAGE_HELP_OPTIONS(24));
	printf(USAGE_MAN_TAIL("fim-inotify(2)"));

	exit(EXIT_SUCCESS);
}

void parseArgs(int argc, char *argv[]) {
	enum {
		OPT_ONLY_DIR = CHAR_MAX + 1,
		OPT_DONT_FOLLOW,
		OPT_EXCLUDE_UNLINK,
		OPT_ONESHOT
	};

	static const struct option longopts[] = {
		{ "help", no_argument, NULL, 'h' },
		{ "version", no_argument, NULL, 'v'},
		{ "pid", required_argument, NULL, 'p' },
		{ "ns", required_argument, NULL, 'n' },
		{ "path", required_argument, NULL, 't'},
		{ "event", optional_argument, NULL, 'e'},
		{ "format", optional_argument, NULL, 'f' },
		{ "only-dir", no_argument, NULL, OPT_ONLY_DIR },
		{ "dont-follow", no_argument, NULL, OPT_DONT_FOLLOW },
		{ "exclude-unlink", no_argument, NULL, OPT_EXCLUDE_UNLINK },
		{ "oneshot", no_argument, NULL, OPT_ONESHOT },
		{ NULL, 0, NULL, 0 }
	};

  int c;

	while ((c = getopt_long(argc, argv, "+hvp:n:t:e::f::", longopts, NULL)) != EOF) {
		switch (c) {
			case 'h':
				usage();
			case 'v':
				printf(UTIL_LINUX_VERSION);
				exit(EXIT_SUCCESS);
			case 'p':
				target_pid = strtoul_or_err(optarg, "failed to parse PID");
				break;
			case 'n':
				target_ns = optarg;
				break;
			case 't':
				target_paths[target_pathc++] = optarg;
				break;
			case 'e':
				if (optarg) {
					if (strcmp(optarg, "all") == 0)         target_events |= IN_ALL_EVENTS;
					else if (strcmp(optarg, "access") == 0) target_events |= IN_ACCESS;
					else if (strcmp(optarg, "modify") == 0) target_events |= IN_MODIFY;
					else if (strcmp(optarg, "attrib") == 0) target_events |= IN_ATTRIB;
					else if (strcmp(optarg, "open") == 0)   target_events |= IN_OPEN;
					else if (strcmp(optarg, "close") == 0)  target_events |= IN_CLOSE;
					else if (strcmp(optarg, "create") == 0) target_events |= IN_CREATE;
					else if (strcmp(optarg, "delete") == 0) target_events |= IN_DELETE;
					else if (strcmp(optarg, "move") == 0)   target_events |= IN_MOVE;
				}
				break;
			case 'f':
				// @TODO: optional formatting
				if (optarg) {
					printf("optarg found: %s", optarg);
				} else {
				}
				break;
			case OPT_ONLY_DIR:
				opt_flags |= IN_ONLYDIR;
				break;
			case OPT_DONT_FOLLOW:
				opt_flags |= IN_DONT_FOLLOW;
				break;
			case OPT_EXCLUDE_UNLINK:
				opt_flags |= IN_EXCL_UNLINK;
				break;
			case OPT_ONESHOT:
				opt_flags |= IN_ONESHOT;
				break;
			default:
				errtryhelp(EXIT_FAILURE);
		}
	}

	// check required arguments exist and are valid
	if (target_pid == -1) {
		errno = EINVAL;
		errexit("no target PID specified for --pid|-p");
	}
	if (target_ns == NULL || *target_ns == '\0') {
		errno = EINVAL;
		errexit("no target namespace specified for --ns|-n");
	}
	if (target_pathc == 0) {
		errno = EINVAL;
		errexit("no target path specified for --path|-t");
	}

	// if target events not set, set defaults
	if (target_events == 0x0) {
		target_events = IN_OPEN | IN_MODIFY;
	}
	// apply optional flags to target events
	target_events |= opt_flags;
}

/**
 * read all available inotify events from the file descriptor `fd`
 * `wd` is the table of watch descriptors for the directories in `paths`
 * `pathc` is the length of `wd` and `paths`
 * `paths` [0->N-1] is the list of watched directories
 */
static void handle_events(int fd, int *wd, int pathc, char *paths[]) {
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
			errexit("read");
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

int main(int argc, char *argv[]) {
	char buf, file[1024];
	int fdns, fdin, i, poll_num;
	int *wd;
	nfds_t nfds;
	struct pollfd fds[1];

  parseArgs(argc, argv);

  // -- JOIN THE NAMESPACE

  // get file descriptor for namespace
	sprintf(file, "/proc/%d/ns/%s", target_pid, target_ns);
  fdns = open(file, O_RDONLY);
  if (fdns == -1) {
    errexit("open");
  }

  // join namespace
  if (setns(fdns, 0) == -1) {
    errexit("setns");
  }

  // close namespace file descriptor
  close(fdns);

  // -- START THE INOTIFY WATCHER

  // create the file descriptor for accessing the inotify API
  fdin = inotify_init1(IN_NONBLOCK);
  if (fdin == -1) {
    errexit("inotify_init1");
  }

  // allocate memory for watch descriptors
  wd = calloc(target_pathc, sizeof(int));
  if (wd == NULL) {
    errexit("calloc");
  }

  // make directories for events
  for (i = 0; i < target_pathc; ++i) {
    wd[i] = inotify_add_watch(fdin, target_paths[i], target_events);
    if (wd[i] == -1) {
      fprintf(stderr, "Cannot watch '%s'\n", target_paths[i]);
      errexit("inotify_add_watch");
    }
  }

  // prepare for polling
  nfds = 1;
  // inotify input
  fds[0].fd = fdin;
  fds[0].events = POLLIN;

  printf("Listening for events.\n");
  fflush(stdout);

  // wait for events
  while (1) {
    poll_num = poll(fds, nfds, -1);
    if (poll_num == -1) {
      if (errno == EINTR) {
        continue;
      }
      errexit("poll");
    }

    if (poll_num > 0) {
      if (fds[0].revents & POLLIN) {
				// inotify events are available
				handle_events(fdin, wd, target_pathc, target_paths);
      }
    }
  }

  printf("Listening for events stopped.\n");
  fflush(stdout);

  // close inotify file descriptor
  close(fdin);
  free(wd);

  exit(EXIT_SUCCESS);
}
