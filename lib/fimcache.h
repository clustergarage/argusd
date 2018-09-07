#ifndef __FIM_CACHE__
#define __FIM_CACHE__

#include <stdbool.h>

#include "fimutil.h"

// @TODO: change all pid lookups to a pid->pid slot [contiguous]

void free_cache(const int pid);
void check_cache_consistency(const int pid, bool only_dir);
void remove_item_from_cache(struct fimwatch *watch, const int index);
int find_watch(const int pid, const int wd);
int find_watch_checked(const int pid, const int wd);
void mark_cache_slot_empty(const int pid, const int slot);
static int find_empty_cache_slot(const int pid);
void add_watch_to_cache(const int pid, const struct fimwatch *watch);
int path_name_to_cache_slot(const int pid, const char *path);
char *wd_to_path_name(const int pid, const int wd);
int wd_to_cache_slot(const int pid, const int wd);

#endif
