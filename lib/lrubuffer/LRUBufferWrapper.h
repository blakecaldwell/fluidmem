/*
 * Copyright 2015 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, May 2015
 */

#ifndef LRUBUFFERWRAPPER_H
#define LRUBUFFERWRAPPER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LRUBuffer LRUBuffer;
typedef struct c_cache_node c_cache_node;

LRUBuffer* newLRUBuffer();

c_cache_node insertCacheNode(LRUBuffer *l, uint64_t key, int ufd);
void referenceCachedNode(LRUBuffer *l, uint64_t key, int ufd);
int popNLRU(LRUBuffer *l, int num_pop, c_cache_node * node_list);
c_cache_node getLRU(LRUBuffer *l);
int isLRUSizeExceeded(LRUBuffer *l);
int getLRUBufferSize(LRUBuffer *l);
int setLRUBufferSize(LRUBuffer *l, int size);
void printLRUBuffer(LRUBuffer *l, FILE * file);
uint64_t * removeUFDFromLRU(LRUBuffer *l, int ufd, int *num_pages);

#ifdef __cplusplus
}
#endif

#endif
