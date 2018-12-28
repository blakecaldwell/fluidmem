/*
 * Copyright 2015 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, May 2015
 */

#ifndef EXTERNRAMCLIENTWRAPPER_H
#define EXTERNRAMCLIENTWRAPPER_H

#include <stdint.h>
#include <stdbool.h>
#include "../../include/usage.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  ramcloud_impl = 0,
  memcached_impl = 1,
  loop_impl = 2,
};

typedef struct externRAMClient externRAMClient;

externRAMClient* newExternRAMClient(int type, char * config, uint64_t upid);

void * writePage(externRAMClient* c, uint64_t key, void ** value);
bool writePages(externRAMClient *c, uint64_t * keys, int num_write, void ** data, int * lengths);
int readPage(externRAMClient *c, uint64_t key, void ** recvBuf);
void readPages(externRAMClient *c, uint64_t * keys, int num_prefetch, void ** recvBufs, int * lengths);
#ifdef ASYNREAD
void readPage_top(externRAMClient *c, uint64_t key, void ** recvBuf);
int readPage_bottom(externRAMClient *c, uint64_t key, void ** recvBuf);
void readPages_top(externRAMClient *c, uint64_t * keys, int num_prefetch, void ** recvBufs, int * lengths);
int readPages_bottom(externRAMClient *c, uint64_t * keys, int num_prefetch, void ** recvBufs, int * lengths);
#endif
bool isFull(externRAMClient *c, uint64_t key);
bool isFullAll(externRAMClient *c);
int getUsage(externRAMClient *c, ServerUsage ** usage);

#ifdef __cplusplus
}
#endif

#endif
