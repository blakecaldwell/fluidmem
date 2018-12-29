/*
 * Copyright 2015 Blake Caldwell, University of Colorado
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, Sep 2016
 */

/*
 * externRAMClientImpl.cc
 *
 * This file implements a concrete class of externRAMClient that interoperates
 * with a C++ map
*/


#include "externRAMClientImpl.hh"
#include <dbg.h>
#include <stdio.h>
#include <cstdint>  // for uint types
#include <iostream>
#include <sstream>
#include <string>
#include <memory>
#include <sys/user.h> /* for PAGE_SIZE */
#ifdef TIMING
#include <timingstats.h>
#endif

#include <boost/shared_ptr.hpp>

using namespace std;

uint64_t upid;

#ifdef DEBUG
uint32_t jenkins_hash(uint8_t *key, size_t len)
{
    uint32_t hash, i;
    for(hash = i = 0; i < len; ++i)
    {
        hash += key[i];
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}
#endif

/*
 *** externRAMClient::create() ****
 * This is the Pointer to Implementation Pattern
 ********************************
 */
externRAMClient * externRAMClient::create(int type, char * config, uint64_t upid_val)
{
  log_trace_in("%s", __func__);
  boost::shared_ptr<externRAMClient> impl(new externRAMClientImpl(),null_deleter());
  log_trace_out("%s", __func__);
  return (impl.get());
}


/*
 *** externRAMClientImpl::externRAMClientImpl() ***
 *  default parameterless constructor
 **********************************************
 */
externRAMClientImpl::externRAMClientImpl()
{
  log_trace_in("%s", __func__);

  kvStore kv;
  pthread_mutex_init(&noop_mutex, NULL);

  log_trace_out("%s", __func__);
}


/*
 *** externRAMClientImpl::~externRAMClientImpl() ***
 *  default parameterless constructor
 **********************************************
 */
externRAMClientImpl::~externRAMClientImpl()
{
  log_trace_in("%s", __func__);

  pthread_mutex_lock(&noop_mutex);
  kv.clear();
  pthread_mutex_unlock(&noop_mutex);

  pthread_mutex_destroy(&noop_mutex);

  log_trace_out("%s", __func__);
}

/*
 *** externRAMClientImpl::write() ***
 *
 * write a key,value pair in RAMCloud
 *
 @ hashcode: unique key
 @ data: buffer pointing to data to be stored in map
 @ size: length in bytes of the data to be stored
 **********************************************
 */
int externRAMClientImpl::write(uint64_t key, void *value, int size) {
  log_trace_in("%s", __func__);
  declare_timers();
  kvStore::iterator it;

  log_debug("%s: got key %p with hash %x", __func__, key, (uint32_t) jenkins_hash((uint8_t *) value, size));
  start_timing_bucket(start, KVWRITE);

  log_lock("%s: locking noop_mutex", __func__);
  pthread_mutex_lock(&noop_mutex);
  log_lock("%s: locked noop_mutex", __func__);

  it = kv.find(key);
  if (it != kv.end()) {
    start_timing_bucket(start, KVCOPY);
    memcpy(it->second, value, PAGE_SIZE);
    stop_timing(start, end, KVCOPY);
  }
  else {
    start_timing_bucket(start, KVCOPY);
    uint8_t * value_toStore = (uint8_t *) malloc(PAGE_SIZE);
    memcpy(value_toStore, value, PAGE_SIZE);
    stop_timing(start, end, KVCOPY);

    kv.emplace(key, value_toStore);
  }

  log_lock("%s: unlocking noop_mutex", __func__);
  pthread_mutex_unlock(&noop_mutex);
  log_lock("%s: unlocked noop_mutex", __func__);

  stop_timing(start, end, KVWRITE);

  log_trace_out("%s", __func__);
  return 0;
}

/*
 *** externRAMClientImpl::read() ***
 *
 * read the data associated with a given key in map
 *
 @ key: unique key
 @ value: already allocated buffer
 -> returns: length of value
 **********************************************
 */
int externRAMClientImpl::read(uint64_t key, void * value) {
  log_trace_in("%s", __func__);
  declare_timers();

  page_data page_value;

  start_timing_bucket(start, KVREAD);
  log_debug("%s: reading page %p", __func__, key);
  log_lock("%s: locking noop_mutex", __func__);
  pthread_mutex_lock(&noop_mutex);
  log_lock("%s: locked noop_mutex", __func__);

  try {
    if (!value) {
      log_err("%s: buffer at %p for key %p has not been allocated", __func__, value, key);
      return -1;
    }
    page_value = kv.at(key);
    stop_timing(start, end, KVREAD);
    start_timing_bucket(start, KVCOPY);
    memcpy(value, page_value, PAGE_SIZE);
  }
  catch (const out_of_range& oor) {
    // not yet found in externram
    log_lock("%s: unlocking noop_mutex", __func__);
    pthread_mutex_unlock(&noop_mutex);
    log_lock("%s: unlocked noop_mutex", __func__);

    stop_timing(start, end, KVCOPY);

    log_debug("%s: read for key %p failed: not found", __func__, key);
    log_trace_out("%s", __func__);
    return 0;
  }

  log_lock("%s: unlocking noop_mutex", __func__);
  pthread_mutex_unlock(&noop_mutex);
  log_lock("%s: unlocked noop_mutex", __func__);
  stop_timing(start, end, KVCOPY);

  log_debug("%s: retrieved key %p with hash %x", __func__, key, (uint32_t) jenkins_hash((uint8_t *) value, PAGE_SIZE));

  log_trace_out("%s", __func__);
  return PAGE_SIZE;
}

#ifdef ASYNREAD
void externRAMClientImpl::read_top(uint64_t key, void * value) {
  return;
}

int externRAMClientImpl::read_bottom(uint64_t key, void * value) {
  log_trace_in("%s", __func__);
  int ret = read(key, value);
  log_trace_out("%s", __func__);
  return ret;
}
#endif

/*
 *** externRAMClientImpl::MultiRead() ***
 *
 * read the data associated with multiple keys sequentially
 *
 @ hashcodes: pointer to an array of unique keys
 @ num_prefetch: number of keys to prefetch with spatial locality
 @ recvBufs: pointer to an array of receive buffers
 @ lengths: pointer to an array of lengths to be recoreded by this function
 -> returns: number of successfully read keys
 **********************************************
 */
int externRAMClientImpl::multiRead(uint64_t * hashcodes, int num_prefetch, void ** recvBufs, int * lengths) {
  int rc = num_prefetch;

  log_trace_in("%s", __func__);

  for( int i=0; i<num_prefetch; i++ )
  {
    recvBufs[i] = malloc(PAGE_SIZE);
    if (!recvBufs[i])
      log_err("%s: failed to allocate multiread buffer", __func__);
    lengths[i] = read( hashcodes[i], recvBufs[i] );
    if (lengths[i] <= 0)
      rc--;
  }
  log_trace_out("%s", __func__);
  return rc;
}

#ifdef ASYNREAD
void externRAMClientImpl::multiRead_top(uint64_t * hashcodes, int num_prefetch, void ** recvBufs, int * lengths) {
  log_trace_in("%s", __func__);
  for( int i=0; i<num_prefetch; i++ )
  {
    recvBufs[i] = malloc(PAGE_SIZE);
    if (!recvBufs[i])
      log_err("%s: failed to allocate multiread buffer", __func__);
  }
  log_trace_out("%s", __func__);
  return;
}

int externRAMClientImpl::multiRead_bottom(uint64_t * hashcodes, int num_prefetch, void ** recvBufs, int * lengths) {
  int rc = num_prefetch;
  log_trace_in("%s", __func__);

  for( int i=0; i<num_prefetch; i++ )
  {
    lengths[i] = read( hashcodes[i], recvBufs[i] );
    if (lengths[i] <= 0)
      rc--;
  }

  log_trace_out("%s", __func__);
  return rc;
}
#endif

/*
 *** externRAMClientImpl::MultiWrite() ***
 *
 * write the data associated with multiple keys seqeuntially
 *
 @ hashcodes: pointer to an array of unique keys
 @ num_write: number of keys to write
 @ data: pointer to an array of buffers to write
 @ lengths: pointer to an array of lengths to write
 -> returns: error code
 **********************************************
 */
int externRAMClientImpl::multiWrite(uint64_t * hashcodes, int num_write, void ** data, int * lengths) {
  log_trace_in("%s", __func__);
  int ret = 0;
  for( int i=0; i<num_write; i++ )
  {
    ret = write( hashcodes[i], data[i], lengths[i] );
    if( ret>0 )
      return ret;
  }
  log_trace_out("%s", __func__);
  return ret;
}

/*
 *** externRAMClientImpl::remove() ***
 *
 * remove the data associated with a given key in map
 *
 @ key: unique key
 -> returns: success on deletion
 **********************************************
 */
int externRAMClientImpl::remove(uint64_t key) {
  log_trace_in("%s", __func__);

  kvStore::iterator it;
  int ret = 0;

  log_lock("%s: locking noop_mutex", __func__);
  pthread_mutex_lock(&noop_mutex);
  log_lock("%s: locked noop_mutex", __func__);

  it = kv.find(key);
  if (it != kv.end()) {
    free(it->second);
    kv.erase(it);
    log_lock("%s: unlocking noop_mutex", __func__);
    pthread_mutex_unlock(&noop_mutex);
    log_lock("%s: unlocked noop_mutex", __func__);
    ret = -1;
  }
  else {
    log_lock("%s: unlocking noop_mutex", __func__);
    pthread_mutex_unlock(&noop_mutex);
    log_lock("%s: unlocked noop_mutex", __func__);

    log_warn("%s: remove for key %p failed: not found", __func__, key);
    ret = 0;
  }

  log_trace_out("%s", __func__);
  return ret;
}
