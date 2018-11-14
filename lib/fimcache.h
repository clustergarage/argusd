/**
 * MIT License
 * 
 * Copyright (c) 2018 ClusterGarage
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __FIM_CACHE__
#define __FIM_CACHE__

#include <stdbool.h>

#include "fimutil.h"

#ifndef ALLOC_INC
#define ALLOC_INC 128
#endif

void free_cache(struct fimwatch *cache);
int find_cached_slot(const int pid, const int sid);
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
