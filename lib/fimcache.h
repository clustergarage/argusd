#ifndef __FIM_CACHE__
#define __FIM_CACHE__

struct watch {
    int wd;                   // watch descriptor (-1 if slot unused)
    char path_name[PATH_MAX]; // cached path name
    uint32_t event_mask;
};

struct watch *wlcache; // array of cached items
int wlcachec;          // current size of the array

void free_cache();
void check_cache_consistency();
int find_watch(int wd);
int find_watch_checked(int wd);
void mark_cache_slot_empty(int slot);
static int find_empty_cache_slot();
int add_watch_to_cache(int wd, const char *path_name, uint32_t event_mask);
int path_name_to_cache_slot(const char *path_name);
static int path_name_in_cache(const char *path_name);

#endif
