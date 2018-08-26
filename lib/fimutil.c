#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <unistd.h>

#include "fimutil.h"

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
