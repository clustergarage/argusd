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
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <unistd.h>

#include "fimutil.h"

/**
 * @TODO: document this
 *
 * @param pid
 * @param ns
 */
void join_namespace(const pid_t pid, const char *ns) {
    char file[1024];
    int fd;

    // Get file descriptor for namespace.
    sprintf(file, "/proc/%d/ns/%s", pid, ns);
    fd = open(file, O_RDONLY | O_CLOEXEC);
    if (fd == EOF) {
#if DEBUG
        perror("open");
#endif
        goto exit;
    }

    // Join namespace.
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
    // Close namespace file descriptor.
    if (fd != EOF) {
        close(fd);
    }
}
