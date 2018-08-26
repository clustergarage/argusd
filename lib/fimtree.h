#ifndef __FIM_TREE__
#define __FIM_TREE__

char **rootpaths; // list of path names supplied
int rootpathc;    // number of path names supplied
int ifd;          // inotify file descriptor
uint32_t imask;   // inotify event mask
bool irecursive;  // inotify event mask

static int ignrootpathc;      // number of path names that we've ceased to monitor
static struct stat *rootstat; // `stat` structures for root directories
static int wlpathc;           // count of directories added to watch list

void copy_root_paths(int pathc, char *paths[]);
char **find_root_path(const char *path);
void remove_root_path(const char *path);
int traverse_tree(const char *path, const struct stat *sb, int tflag, struct FTW *ftwbuf);
int watch_path(const char *path);
int watch_path_recursive(const char *path);
void watch_subtree(int fd, char *path, uint32_t mask, bool recursive);
void rewrite_cached_paths(const char *oldpathpf, const char *oldname, const char *newpathpf, const char *newname);
int remove_subtree(int fd, char *path);

#endif
