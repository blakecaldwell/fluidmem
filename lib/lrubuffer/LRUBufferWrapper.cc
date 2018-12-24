/*
 * Copyright 2015 Blake Caldwell, University of Colorado
 * Copyright 2015 Youngbin Im, University of Colorado
 * All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, May 2015
 */

/*
 * LRUBufferWrapper is a C wrapper to the C++ class LRUBuffer
 */

#include "LRUBufferWrapper.h"
#include "LRUBuffer.hh"
#include <stdio.h>

extern "C" {
  LRUBuffer * lrubuf;
  LRUBuffer * newLRUBuffer() {
    lrubuf = LRUBuffer::create();
    return lrubuf;
  }

  void referenceCachedNode(LRUBuffer *l, uint64_t key, int ufd) {
    l->referenceCachedNode(key, ufd);
  }
  struct c_cache_node insertCacheNode(LRUBuffer *l, uint64_t key, int ufd) {
    return l->insertCacheNode(key, ufd);
  }
  int popNLRU(LRUBuffer *l, int num_pop, c_cache_node ** node_list) {
    return l->popNLRU(num_pop, node_list);
  }
  c_cache_node getLRU(LRUBuffer *l) {
    return l->getLRU();
  }
  int isLRUSizeExceeded(LRUBuffer *l) {
    return l->isLRUSizeExceeded();
  }
  int getLRUBufferSize(LRUBuffer *l) {
    return l->getSize();
  }
  int setLRUBufferSize(LRUBuffer *l, int size) {
    return l->setSize(size);
  }
  uint64_t * removeUFDFromLRU(LRUBuffer *l, int ufd, int * numPages) {
    return l->removeUFDFromLRU(ufd, numPages);
  }
  void printLRUBuffer(LRUBuffer *l, FILE * file) {
    l->printLRUBuffer(file);
  }
}
