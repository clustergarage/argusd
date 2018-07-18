#ifndef __FIM_INOTIFY__
#define __FIM_INOTIFY__

#include <unistd.h>

#define DEBUG 0
#define FIMNOTIFY_KILL SIGKILL

static void handle_events(int fd, int *wd, int pathc, char *paths[]);
void join_namespace(const pid_t pid, const char *ns);
int start_inotify_watcher(int pathc, char *paths[], uint32_t event_mask, int process_eventfd);

#endif
