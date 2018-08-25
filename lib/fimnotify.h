#ifndef __FIM_NOTIFY__
#define __FIM_NOTIFY__

#define DEBUG 0
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

static int reinitialize(int oldfd, uint32_t mask);
static size_t process_next_inotify_event(int *fd, char *buf, int len, int first, mqd_t mq);
static void process_inotify_events(int *fd, int processevtfd);
int start_inotify_watcher(int pathc, char *paths[], uint32_t mask, mqd_t mq);

#endif
