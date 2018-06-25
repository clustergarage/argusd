#define _GNU_SOURCE
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>

#include "lib_fiminotify.h"

#define __attribute__(_arg_)

#define USAGE_HEADER    "\nUsage:\n"
#define USAGE_OPTIONS   "\nOptions:\n"
#define USAGE_SEPARATOR "\n"
#define USAGE_HELP_OPTIONS(marg_dsc)       \
    "%-" #marg_dsc "s%s\n"                 \
    "%-" #marg_dsc "s%s\n",                \
    " -h, --help",    "display this help", \
    " -v, --version", "display version"
#define USAGE_MAN_TAIL(_man) "\nFor more details see %s.\n", _man
#define UTIL_LINUX_VERSION "%s v1.0\n", program_invocation_short_name

#define errtryhelp(eval) __extension__ ({                      \
    fprintf(stderr, "Try '%s --help' for more information.\n", \
        program_invocation_short_name);                        \
    exit(eval);                                                \
})

static pid_t target_pid = -1;
static char *target_ns = NULL;
static char *target_paths[32] = {NULL};
static unsigned int target_pathc = 0;
static unsigned int target_event_mask;

static void __attribute__((__noreturn__)) usage(void) {
    FILE *out = stdout;

    fputs(USAGE_HEADER, out);
    fprintf(out, " %s -p<pid> -n<namespace> -t<path>... [-e<event>...] [-f<format>]\n", program_invocation_short_name);

    fputs(USAGE_SEPARATOR, out);
    fputs("Watch for namespace events within paths of a target PID.\n", out);

    fputs(USAGE_OPTIONS, out);
    fputs(" -p, --pid <pid>          target PID to watch\n", out);
    fputs(" -n, --ns <namespace>     target namespace {ipc|net|mnt|pid|user|uts}\n", out);
    fputs(" -t, --path <path>        target watch path(s)\n", out);
    fputs(" -e, --event <event>      event to watch {access|modify|attrib|open|close|create|delete|move|all}\n", out);
    fputs("     --only-dir           only watch path if it is a directory\n", out);
    fputs("     --dont-follow        do not follow a symlink\n", out);
    fputs("     --exclude-unlink     exclude events on unlinked objects\n", out);
    fputs("     --oneshot            only send event once\n", out);
    fputs(" -f, --format <format>    custom log format\n", out);

    fputs(USAGE_SEPARATOR, out);
    printf(USAGE_HELP_OPTIONS(24));
    printf(USAGE_MAN_TAIL("fim_inotify(2)"));

    exit(EXIT_SUCCESS);
}

unsigned long strtoul_or_err(const char *str, const char *errmsg) {
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
    errexit(errmsg);
}

void parse_args(int argc, char *argv[]) {
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
    unsigned int opt_flags;

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
                    if (strcmp(optarg, "all") == 0)         target_event_mask |= IN_ALL_EVENTS;
                    else if (strcmp(optarg, "access") == 0) target_event_mask |= IN_ACCESS;
                    else if (strcmp(optarg, "modify") == 0) target_event_mask |= IN_MODIFY;
                    else if (strcmp(optarg, "attrib") == 0) target_event_mask |= IN_ATTRIB;
                    else if (strcmp(optarg, "open") == 0)   target_event_mask |= IN_OPEN;
                    else if (strcmp(optarg, "close") == 0)  target_event_mask |= IN_CLOSE;
                    else if (strcmp(optarg, "create") == 0) target_event_mask |= IN_CREATE;
                    else if (strcmp(optarg, "delete") == 0) target_event_mask |= IN_DELETE;
                    else if (strcmp(optarg, "move") == 0)   target_event_mask |= IN_MOVE;
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
            // @TODO: add option for IN_MASK_ADD
            //
            // If a watch instance already exists for the filesystem
            // object corresponding to pathname, add (OR) the events in
            // mask to the watch mask (instead of replacing the mask).
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
    if (target_event_mask == 0x0) {
        target_event_mask = IN_OPEN | IN_MODIFY;
    }
    // apply optional flags to target events
    target_event_mask |= opt_flags;
}

int main(int argc, char *argv[]) {
    parse_args(argc, argv);

    join_namespace(target_pid, target_ns);
    start_inotify_watcher(target_pathc, target_paths, target_event_mask);

    exit(EXIT_SUCCESS);
}

