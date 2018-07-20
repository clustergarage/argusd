#ifndef __FIM_INOTIFY__
#define __FIM_INOTIFY__

#include <unistd.h>

#define DEBUG 0
#define FIMNOTIFY_KILL SIGKILL
#define MQ_MAX_SIZE 1024
#define MQ_QUEUE_NAME "/fw_mqueue"
#define MQ_EXIT_MESSAGE "exit"

struct fimwatch_event {
    uint32_t event_mask;
    const char *path_name, *file_name;
    bool is_dir;
};

static void handle_events(int fd, int *wd, int pathc, char *paths[], int process_eventfd);
void join_namespace(const pid_t pid, const char *ns);
int start_inotify_watcher(int pathc, char *paths[], uint32_t event_mask, mqd_t mq);

#endif
