/*
 * Copyright 2015 Blake Caldwell, University of Colorado
 * Copyright 2015 Youngbin Im, University of Colorado
 * All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, May 2015
 */

#include "externRAMClientWrapper.h"
#include "externRAMClient.hh"
#include <stdio.h>
#include <sys/user.h>
#include <unistd.h>

// This should be removed when dbg.h can be included in this file externRAMClientWrapper.cc
// Currently including dbg.h produces a linking error.
#define log_recoverable_err(M, ...) {fprintf(stderr, "[ERROR] (%s:%d: ) " M "\n", __FILE__, __LINE__, ##__VA_ARGS__);}

extern "C" {
  externRAMClient * client;
  externRAMClient * newExternRAMClient(int type, char * config, uint64_t upid) {
    client = externRAMClient::create(type, config, upid);
    return client;
  }

  void * writePage(externRAMClient *c, uint64_t key, void ** value) {
    int size;
    void * ret;
    int err = 0;
    int cnt = 0;
    size = PAGE_SIZE;
    while( true ) {
      ret = c->write(key, value, size, &err);
      if( err < 0 ) {
        if( cnt%NUM_ERRORS_TO_CHECK_ISFULL==0 && isFull(c, key) )
          log_recoverable_err( "The externRAM is full." );
        sleep(1);
      }
      else {
        break;
      }
      cnt++;
    }
    return ret;
  }

  bool writePages(externRAMClient *c, uint64_t * keys, int num_write, void ** data, int * lengths) {
    bool ret;
    int err = 0;
    int cnt = 0;
    while( true ) {
      ret = c->multiWrite(keys, num_write, data, lengths, &err);
      if( err < 0 ) {
        if( cnt%NUM_ERRORS_TO_CHECK_ISFULL==0 && isFullAll(c) )
          log_recoverable_err( "The externRAM is full." );
        sleep(1);
      }
      else {
        break;
      }
      cnt++;
    }
    return ret;
  }

  int readPage(externRAMClient *c, uint64_t key, void ** recvBuf) {
    return c->read(key,recvBuf);
  }

  void readPages(externRAMClient *c, uint64_t * keys, int num_prefetch, void ** recvBufs, int * lengths) {
    c->multiRead(keys, num_prefetch, recvBufs, lengths);
  }

#ifdef ASYNREAD
  void readPage_top(externRAMClient *c, uint64_t key, void ** recvBuf) {
    c->read_top(key,recvBuf);
  }
  int readPage_bottom(externRAMClient *c, uint64_t key, void ** recvBuf) {
    return c->read_bottom(key,recvBuf);
  }
  void readPages_top(externRAMClient *c, uint64_t * keys, int num_prefetch, void ** recvBufs, int * lengths) {
    c->multiRead_top(keys, num_prefetch, recvBufs, lengths);
  }
  int readPages_bottom(externRAMClient *c, uint64_t * keys, int num_prefetch, void ** recvBufs, int * lengths) {
    return c->multiRead_bottom(keys, num_prefetch, recvBufs, lengths);
  }
#endif

  bool isFull(externRAMClient *c, uint64_t key) {
    return c->isFull(key);
  }

  bool isFullAll(externRAMClient *c) {
    return c->isFullAll();
  }

  int getUsage(externRAMClient *c, ServerUsage ** usage) {
    return c->getUsage(usage);
  }
}
