#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <unistd.h>

#define __attribute__(_arg_)

#define USAGE_HEADER    "\nUsage:\n"
#define USAGE_OPTIONS   "\nOptions:\n"
#define USAGE_SEPARATOR "\n"
#define USAGE_HELP_OPTIONS(marg_dsc)     \
	"%-" #marg_dsc "s%s\n"                 \
	"%-" #marg_dsc "s%s\n",                \
	" -h, --help",    "display this help", \
	" -v, --version", "display version"
#define USAGE_MAN_TAIL(_man) "\nFor more details see %s.\n", _man
#define UTIL_LINUX_VERSION "%s from v1.0\n", program_invocation_short_name/*, PACKAGE_STRING*/

#define errtryhelp(eval) __extension__ ({ \
	fprintf(stderr, "Try '%s --help' for more information.\n", \
		program_invocation_short_name);                          \
	exit(eval); \
})

#define errexit(msg) do { \
	perror(msg);            \
	exit(EXIT_FAILURE);     \
} while (0)

static void __attribute__((__noreturn__)) usage(void) {
	FILE *out = stdout;

	fputs(USAGE_HEADER, out);
	fprintf(out, " %s [options] [<program> [<argument>...]]\n", program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs("Run a program with namespaces of other processes.\n", out);

	fputs(USAGE_OPTIONS, out);
	fputs(" -p, --pid <pid>        foo bar baz\n", out);
	fputs(" -n, --ns <namespace>   foo bar baz\n", out);
	fputs(" -t, --paths <paths...> foo bar baz\n", out);
	fputs(" -f, --format <format>  foo bar baz\n", out);

	fputs(USAGE_SEPARATOR, out);
	printf(USAGE_HELP_OPTIONS(24));
	printf(USAGE_MAN_TAIL("fim-inotify(2)"));

	exit(EXIT_SUCCESS);
}

unsigned long strtoul_or_err(const char *str, const char *errmesg) {
	unsigned long num;
	char *end = NULL;

	errno = 0;
	if (str == NULL || *str == '\0') {
		goto err;
	}
	num = strtoul(str, &end, 10);

	if (errno || str == end || (end && *end)) {
		goto err;
	}
	return num;
err:
	errexit(errmesg);
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

			// @TODO: make events to watch configurable

			// print event type
			if (event->mask & IN_OPEN) {
				fprintf(stdout, "IN_OPEN: ");
			}
			if (event->mask & IN_MODIFY) {
				fprintf(stdout, "IN_MODIFY: ");
			}

			// print the name of the watched directory
			for (i = 0; i < pathc; i++) {
				if (wd[i] == event->wd) {
					fprintf(stdout, "%s/", paths[i]);
					break;
				}
			}

			// print the name of the file
			if (event->len) {
				fprintf(stdout, "%s", event->name);
			}

			// @TODO: make file|directory watch configurable

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
	enum { OPT_PRESERVE_CRED = CHAR_MAX + 1 };

	static const struct option longopts[] = {
		{ "help", no_argument, NULL, 'h' },
		{ "version", no_argument, NULL, 'v'},
		{ "pid", required_argument, NULL, 'p' },
		{ "ns", required_argument, NULL, 'n' },
		{ "paths", required_argument, NULL, 't'},
		{ "format", optional_argument, NULL, 'f' },
		{ NULL, 0, NULL, 0 }
	};

	pid_t target_pid = -1;
  char *target_ns = NULL;
	char *target_paths[32] = {NULL};
	int tpc = 0;
	int c;

  // -- PARSE ARGUMENTS

	while ((c = getopt_long(argc, argv, "+hvp:n:t:f::", longopts, NULL)) != -1) {
		switch (c) {
			case 'h':
				usage();
			case 'v':
				printf(UTIL_LINUX_VERSION);
				return EXIT_SUCCESS;
			case 'p':
				target_pid = strtoul_or_err(optarg, "failed to parse PID");
				break;
			case 'n':
				target_ns = optarg;
				break;
			case 't':
				target_paths[tpc++] = optarg;
				break;
			case 'f':
				if (optarg) {
					printf("optarg found: %s", optarg);
				} else {
				}
				break;
			default:
				errtryhelp(EXIT_FAILURE);
		}
	}

	if (target_pid == -1) {
		errexit("no target PID specified for --pid");
	}
	if (target_ns == NULL || *target_ns == '\0') {
		errexit("no target namespace specified for --ns");
	}
	if (tpc == 0) {
		errexit("no target paths specified for --paths");
	}

	char buf, file[1024];
	int fdns, fdin, i, poll_num;
	int *wd;
	nfds_t nfds;
	struct pollfd fds[1];

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
  wd = calloc(tpc, sizeof(int));
  if (wd == NULL) {
    errexit("calloc");
  }

  /**
   * make directories for events
   * - file was opened
   * - file was modified
   */
	// @TODO: split on ; on paths argument, and loop for multi-directory watchers
  for (i = 0; i < tpc; i++) {
		// @TODO: make events configurable
    wd[i] = inotify_add_watch(fdin, target_paths[i], IN_OPEN | IN_MODIFY);
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

  // wait for events and/or terminal input
  fprintf(stdout, "Listening for events.\n");
  fflush(stdout);

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
				handle_events(fdin, wd, tpc, target_paths);
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
