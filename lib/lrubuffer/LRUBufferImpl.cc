/*
 * Copyright 2016 Blake Caldwell, University of Colorado
 * Copyright 2016 Youngbin Im, University of Colorado
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, May 2015
 */

/*
 * LRUBufferImpl.cc
 *
 * This file implements a concrete class of LRUBuffer
*/

#include "LRUBufferImpl.hh"
#include <dbg.h>
#include <pagehash.h>
#include <stdio.h>
#include <malloc.h>
#include <vector>
#include <cstdint>  // for uint types
#include <boost/shared_ptr.hpp>
#include <monitorstats.h>

/*
 *** LRUBuffer::create() ****
 * This is the Pointer to Implementation Pattern
 ********************************
 */
LRUBuffer * LRUBuffer::create()
{
  log_trace_in("%s", __func__);
  /*
   * Necesssary to use null_deleter or object will get implicitly deleted.
   */
  boost::shared_ptr<LRUBuffer> impl(new LRUBufferImpl(),null_deleter());

  log_trace_out("%s", __func__);
  return (impl.get());
}


/*
 *** LRUBufferImpl::LRUBufferImpl() ***
 *  default parameterless constructor
 **********************************************
 */
LRUBufferImpl::LRUBufferImpl()
{
#ifdef MONITORSTATS
  StatsSetLRUBufferCap((unsigned long)this->getMaxSize());
  StatsSetLRUBufferSize(0);
#endif

  log_debug("%s: created instance of LRU Buffer", __func__);
}


/*
 *** LRUBufferImpl::~LRUBufferImpl() ***
 *  default parameterless constructor
 **********************************************
 */
LRUBufferImpl::~LRUBufferImpl()
{
  log_debug("%s: deleted instance of LRU Buffer", __func__);
}

void LRUBufferImpl::referenceCachedNode(uint64_t key, int ufd) {
  /**** NOT USED ****/
  log_trace_in("%s", __func__);

  uint64_t hashcode;

  hashcode = hash_page_key(key, ufd);

  // move the referenced cache to the front
  cache.insert(*cache.find(hashcode));

  log_trace_out("%s", __func__);
}

c_cache_node LRUBufferImpl::insertCacheNode(uint64_t key, int ufd, bool evict) {
  log_trace_in("%s", __func__);

  cache_node node;
  c_cache_node return_node;
  memset(&return_node, 0, sizeof(c_cache_node));

  node.hashcode = hash_page_key(key, ufd);
  node.ufd = ufd;

  cache.insert(node);

#ifdef MONITORSTATS
  StatsIncrLRUBufferSize();
#endif

  if (evict && isLRUSizeExceeded()) {
    log_debug("%s: LRU size exceeded. Calling popLRU", __func__);
    return_node = popLRU();
  }

#ifdef DEBUG
  int new_size = getSize();
  log_debug("%s: new LRU size is %d", __func__, new_size);
#endif
  log_trace_out("%s", __func__);
  return return_node;
}

int LRUBufferImpl::popNLRU(int num_pop, c_cache_node ** node_list) {
  log_trace_in("%s", __func__);

  int i = 0;
  int lru_size = getSize();

  if (num_pop > lru_size)
    num_pop = lru_size;

  *node_list = (c_cache_node *) malloc(sizeof(c_cache_node) * num_pop);

  for (i = 0; i < num_pop; i++) {
    c_cache_node node = popLRU();
    (*node_list)[i].ufd = node.ufd;
    (*node_list)[i].hashcode = node.hashcode;
  }

  log_trace_out("%s", __func__);
  return num_pop;
}

c_cache_node LRUBufferImpl::popLRU() {
  log_trace_in("%s", __func__);

  uint64_t key;
  cache_node node = cache.back();
  c_cache_node return_node;
  key = node.hashcode;
  cache.pop_back();

#ifdef MONITORSTATS
  StatsDecrLRUBufferSize();
#endif

  return_node.hashcode = node.hashcode;
  return_node.ufd = node.ufd;

  log_trace_out("%s", __func__);
  return return_node;
}

struct c_cache_node LRUBufferImpl::getLRU() {
  log_trace_in("%s", __func__);

  struct c_cache_node ret;
  cache_node node = cache.back();
  ret.hashcode = node.hashcode;
  ret.ufd = node.ufd;

  log_trace_out("%s", __func__);
  return ret;
}


int LRUBufferImpl::isLRUSizeExceeded() {
  int ret;
  log_trace_in("%s", __func__);
  if( cache.isSizeExceeded() )
    ret = 1;
  else
    ret = 0;
  log_trace_out("%s", __func__);
  return ret;
}

int LRUBufferImpl::getSize() {
  int ret;
  log_trace_in("%s", __func__);
  ret = cache.getSize();
  log_trace_out("%s", __func__);
  return ret;
}

int LRUBufferImpl::getMaxSize() {
  int ret;
  log_trace_in("%s", __func__);
  ret = cache.getMaxSize();
  log_trace_out("%s", __func__);
  return ret;
}

int LRUBufferImpl::setSize(int size) {
  log_trace_in("%s", __func__);

  int ret;
  ret = cache.setSize(size);

#ifdef MONITORSTATS
  StatsSetLRUBufferCap((unsigned long)size);
#endif

  log_trace_out("%s", __func__);
  return ret;
}

uint64_t * LRUBufferImpl::removeUFDFromLRU(int ufd, int *numPages) {
  log_trace_in("%s", __func__);

  std::vector<uint64_t> keyVector;
  uint64_t *keyList;

  lru_list::iterator itr = cache.begin();
  int i=0;
  for ( ; itr!= cache.end(); itr++, i++ ) {
    if (itr->ufd == ufd) {
      // this cache_node will be removed
      keyVector.push_back(itr->hashcode & (uint64_t)(PAGE_MASK));
      cache.erase(itr->hashcode);
    }
  }
  *numPages = keyVector.size();

  // populate the memory region to be returned to libuserfault
  keyList = (uint64_t *)malloc(keyVector.size() * sizeof(uint64_t));
  for(std::vector<uint64_t>::size_type i = 0; i != keyVector.size(); i++) {
     memcpy(&keyList[i], &keyVector[i], sizeof(uint64_t));
  }

#ifdef MONITORSTATS
  StatsSetLRUBufferSize((unsigned long)this->getSize());
#endif

  log_debug("%s: new LRU size is %d", __func__, (unsigned long)getSize());
  log_trace_out("%s", __func__);
  return keyList;
}

void LRUBufferImpl::printLRUBuffer(FILE * file) {
  cache.printCache("after", __func__, file);
}

