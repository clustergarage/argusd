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

#ifndef __ARGUS_TREE__
#define __ARGUS_TREE__

#include "argusutil.h"

void copy_root_paths(struct arguswatch *watch);
char **find_root_path(const struct arguswatch *watch, const char *path);
char **find_path(const struct arguswatch *watch, const char *path);
void remove_root_path(struct arguswatch *watch, const char *path);
bool should_ignore_path(struct arguswatch *watch, const char *path);
int watch_path(struct arguswatch *watch, const char *path);
int watch_path_recursive(struct arguswatch *watch, const char *path);
void watch_subtree(struct arguswatch *watch);
void rewrite_cached_paths(const struct arguswatch *watch, const char *oldpathpf, const char *oldname, const char *newpathpf, const char *newname);
int remove_subtree(const struct arguswatch *watch, char *path);

#endif
