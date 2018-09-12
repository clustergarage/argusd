#ifndef __FIM_CACHE__
#define __FIM_CACHE__

#include <stdbool.h>

#include "fimutil.h"

#ifndef ALLOC_INC
#define ALLOC_INC 128
#endif

int find_cached_slot(const int pid, const int sid);
void free_cache(struct fimwatch *cache);
void check_cache_consistency(const struct fimwatch *watch);
void remove_item_from_cache(struct fimwatch *watch, const int index);
int find_watch(const struct fimwatch *watch, const int wd);
int find_watch_checked(const struct fimwatch *watch, const int wd);
void mark_cache_slot_empty(const int slot);
static int find_empty_cache_slot();
void add_watch_to_cache(struct fimwatch *watch);
int path_name_to_cache_slot(const struct fimwatch *watch, const char *path);
char *wd_to_path_name(const struct fimwatch *watch, const int wd);
int wd_to_cache_slot(const struct fimwatch *watch, const int wd);

#endif
