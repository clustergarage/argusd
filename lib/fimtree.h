#ifndef __FIM_TREE__
#define __FIM_TREE__

#include "fimutil.h"

void copy_root_paths(struct fimwatch *watch);
char **find_root_path(const struct fimwatch *watch, const char *path);
void remove_root_path(struct fimwatch *watch, const char *path);
bool should_ignore_path(struct fimwatch *watch, const char *path);
int watch_path(struct fimwatch *watch, const char *path);
int watch_path_recursive(struct fimwatch *watch, const char *path);
void watch_subtree(struct fimwatch *watch);
void rewrite_cached_paths(const struct fimwatch *watch, const char *oldpathpf, const char *oldname, const char *newpathpf, const char *newname);
int remove_subtree(const struct fimwatch *watch, char *path);

#endif
