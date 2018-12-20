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

#ifndef __ARGUS_NOTIFY__
#define __ARGUS_NOTIFY__

#include <limits.h>
#include <signal.h>
#include <sys/inotify.h>

#include "argusutil.h"

#ifndef ARGUSNOTIFY_KILL
#define ARGUSNOTIFY_KILL SIGKILL
#endif

static int reinitialize(struct arguswatch **watch);
static size_t process_next_inotify_event(struct arguswatch **watch, const struct inotify_event *event, ssize_t len,
    bool first, arguswatch_logfn logfn);
static void process_inotify_events(struct arguswatch **watch, arguswatch_logfn logfn);
int start_inotify_watcher(const char *name, const char *nodename, const char *podname, int pid, int sid,
    unsigned int pathc, const char *paths[], unsigned int ignorec, const char *ignores[], uint32_t mask, bool onlydir,
    bool recursive, int maxdepth, bool followmove, int processevtfd, const char *tags, const char *logformat,
    arguswatch_logfn logfn);
void send_watcher_kill_signal(const int *const processfd);

#endif
