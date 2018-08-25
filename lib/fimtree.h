#ifndef __FIM_TREE__
#define __FIM_TREE__

char **root_paths;                  // list of path names supplied on command line
int root_pathc;                     // number of path names supplied on command line
static int ignored_root_pathc;      // number of command-line path names that we've ceased to monitor
static struct stat *root_path_stat; // `stat` structures for root directories
static int wlpathc;                 // count of directories added to watch list
static int ifd;                     // inotify file descriptor
static uint32_t imask;              // inotify event mask

void copy_root_paths(int pathc, char *paths[]);
char **find_root_path(const char *path);
void remove_root_path(const char *path);
int traverse_tree(const char *path, const struct stat *sb, int tflag, struct FTW *ftwbuf);
int watch_path(int fd, const char *path, uint32_t event_mask);
void watch_subtree(int fd, char *path, uint32_t event_mask);
void rewrite_cached_paths(const char *old_path_prefix, const char *old_name, const char *new_path_prefix, const char *new_name);
int remove_subtree(int fd, char *path);

#endif
