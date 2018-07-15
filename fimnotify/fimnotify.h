#ifndef __FIM_INOTIFY__
#define __FIM_INOTIFY__

#include <unistd.h>

#define errexit(msg) do { \
    perror(msg);          \
    exit(EXIT_FAILURE);   \
} while (0)

#define FIMNOTIFY_KILL 9

static void handle_events(int fd, int *wd, int pathc, char *paths[]);
void join_namespace(const pid_t pid, const char *ns);
void start_inotify_watcher(int pathc, char *paths[], uint32_t event_mask, int process_eventfd);

#endif
