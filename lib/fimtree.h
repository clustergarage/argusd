#ifndef __FIM_TREE__
#define __FIM_TREE__

#include "fimutil.h"

char **rootpaths[WATCH_MAX]; // list of path names supplied
int rootpathc[WATCH_MAX];    // number of path names supplied
int rootmask[WATCH_MAX];
bool rootrecursive[WATCH_MAX];
struct fimwatch *iwatch;
int ipid;                    // pid of container
int ifd;

static struct stat *rootstat[WATCH_MAX]; // `stat` structures for root directories
static int ipathc;
static int ignrootpathc[WATCH_MAX];      // number of path names that we've ceased to monitor

void copy_root_paths(const int pid, int pathc, char *paths[]);
char **find_root_path(const int pid, const char *path);
void remove_root_path(const int pid, const char *path);
int traverse_tree(const char *path, const struct stat *sb, int tflag, struct FTW *ftwbuf);
int watch_path(const char *path);
int watch_path_recursive(const char *path);
void watch_subtree(const int pid, struct fimwatch *watch);
void rewrite_cached_paths(const int pid, const char *oldpathpf, const char *oldname, const char *newpathpf, const char *newname);
int remove_subtree(const int pid, int fd, char *path);

#endif
