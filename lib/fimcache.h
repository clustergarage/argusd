#ifndef __FIM_CACHE__
#define __FIM_CACHE__

#include <stdbool.h>

struct fimwatch {
    int wd;                   // watch descriptor (-1 if slot unused)
    char path_name[PATH_MAX]; // cached path name
    uint32_t event_mask;
    bool recursive;
};

struct fimwatch *wlcache; // array of cached items
int wlcachec;             // current size of the array

void free_cache();
void check_cache_consistency();
int find_watch(int wd);
int find_watch_checked(int wd);
void mark_cache_slot_empty(int slot);
static int find_empty_cache_slot();
int add_watch_to_cache(int wd, const char *path, uint32_t mask, bool recursive);
int path_name_to_cache_slot(const char *path);
static int path_name_in_cache(const char *path);

#endif
