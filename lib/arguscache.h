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

#ifndef __ARGUS_CACHE__
#define __ARGUS_CACHE__

#include <stdbool.h>

#include "argusutil.h"

#ifndef ALLOC_INC
#define ALLOC_INC 32
#endif

void clear_watch(struct arguswatch **watch);
int find_cached_slot(int pid, int sid);
void check_cache_consistency(struct arguswatch **watch);
static void remove_item_from_cache(struct arguswatch **watch, int index);
int find_watch(const struct arguswatch *watch, int wd);
int find_watch_checked(const struct arguswatch *watch, int wd);
void mark_cache_slot_empty(int slot);
static int find_empty_cache_slot();
void add_watch_to_cache(struct arguswatch **watch);
int path_name_to_cache_slot(const struct arguswatch *watch, const char *path);
const char *wd_to_path_name(const struct arguswatch *watch, int wd);

#endif
