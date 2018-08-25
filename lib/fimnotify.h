#ifndef __FIM_INOTIFY__
#define __FIM_INOTIFY__

#include <unistd.h>

/**
 * @TODO: split out into separate, logical files
 * - caching
 * - paths
 * - tree traversal
 * - utils (join namespace, ...)
 */

#define DEBUG 0
#define FIMNOTIFY_KILL SIGKILL
#define MQ_MAX_SIZE 1024
#define MQ_QUEUE_NAME "/fw_mqueue"
#define MQ_EXIT_MESSAGE "exit"

struct watch {
    int wd;                   // watch descriptor (-1 if slot unused)
    char path_name[PATH_MAX]; // cached path name
    uint32_t event_mask;
};

static void free_cache();
static void check_cache_consistency();
static int find_watch(int wd);
static int find_watch_checked(int wd);
static void mark_cache_slot_empty(int slot);
static int find_empty_cache_slot();
static int add_watch_to_cache(int wd, const char *path_name, uint32_t event_mask);
static int path_name_to_cache_slot(const char *path_name);
static int path_name_in_cache(const char *path_name);

///////////////////////////////////////////////////////////////////////////////

static void copy_root_paths(int pathc, char *paths[]);
static char **find_root_path(const char *path);
static int is_root_path(const char *path);
static void remove_root_path(const char *path);

///////////////////////////////////////////////////////////////////////////////

static int traverse_tree(const char *path, const struct stat *sb, int tflag, struct FTW *ftwbuf);
static int watch_path(int fd, const char *path, uint32_t event_mask);
static void watch_subtree(int fd, char *path, uint32_t event_mask);
static void rewrite_cached_paths(const char *old_path_prefix, const char *old_name, const char *new_path_prefix, const char *new_name);
static int remove_subtree(int fd, char *path);
static int reinitialize(int old_fd, uint32_t event_mask);

///////////////////////////////////////////////////////////////////////////////

struct fimwatch_event {
    uint32_t event_mask;
    const char *path_name, *file_name;
    bool is_dir;
};

static int process_next_inotify_event(int *fd, char *buf, int len, int first, mqd_t mq);
static void process_inotify_events(int *fd, /*int pathc, char *paths[], */int process_eventfd);
int start_inotify_watcher(int pathc, char *paths[], uint32_t event_mask, mqd_t mq);

///////////////////////////////////////////////////////////////////////////////

void join_namespace(const pid_t pid, const char *ns);

#endif
