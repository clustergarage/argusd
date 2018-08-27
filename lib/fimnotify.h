#ifndef __FIM_NOTIFY__
#define __FIM_NOTIFY__

#include "fimutil.h"

#define FIMNOTIFY_KILL SIGKILL
#define MQ_MAX_SIZE 1024
#define MQ_QUEUE_NAME "/fw_mqueue"
#define MQ_EXIT_MESSAGE "exit"

static const int INOTIFY_READ_BUF_LEN = (100 * (sizeof(struct inotify_event) + NAME_MAX + 1));

struct fimwatch_event {
    uint32_t event_mask;
    const char *path_name, *file_name;
    bool is_dir;
};

static int reinitialize(const int pid, struct fimwatch *watch);
static size_t process_next_inotify_event(const int pid, int *fd, char *buf, int len, int first);
static void process_inotify_events(const int pid, int *fd);
// @TODO: more consts?
int start_inotify_watcher(const int pid, int pathc, char *paths[], uint32_t mask, bool recursive, int processevtfd, mqd_t mq);

#endif
