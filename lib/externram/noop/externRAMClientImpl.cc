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
  log_debug("%s: got key %p with hash %x", __func__, key, (uint32_t) jenkins_hash((uint8_t *) value, size));
  uint8_t * value_toStore = (uint8_t *) malloc(PAGE_SIZE);
  memcpy(value_toStore, value, PAGE_SIZE);
  pthread_mutex_lock(&noop_mutex);
  kv[key] = value_toStore;
  pthread_mutex_unlock(&noop_mutex);

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

  page_data page_value;

  log_debug("%s: reading page %p", __func__, key);
  pthread_mutex_lock(&noop_mutex);
  try {
    page_value = kv.at(key);
  }
  catch (const out_of_range& oor) {
    pthread_mutex_unlock(&noop_mutex);
    log_debug("%s: read for key %p failed: not found", __func__, key);
    log_trace_out("%s", __func__);
    return 0;
  }

  memcpy(value, page_value, PAGE_SIZE);
  pthread_mutex_unlock(&noop_mutex);
  log_debug("%s: retrieved key %p with hash %x", __func__, key, (uint32_t) jenkins_hash((uint8_t *) value, PAGE_SIZE));

  log_trace_out("%s", __func__);
  return PAGE_SIZE;
}

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
    lengths[i] = read( hashcodes[i], recvBufs[i] );
    if (lengths[i] <= 0)
      rc--;
  }
  log_trace_out("%s", __func__);
  return rc;
}

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

  int ret = 0;

  pthread_mutex_lock(&noop_mutex);
  page_data page_value;
  try {
    page_value = kv.at(key);
  }
  catch (const out_of_range& oor) {
    pthread_mutex_unlock(&noop_mutex);
    log_warn("%s: remove for key %p failed: not found", __func__, key);
    log_trace_out("%s", __func__);
    return 0;
  }

  ret = kv.erase(key);
  pthread_mutex_unlock(&noop_mutex);

  free(page_value);

  log_trace_out("%s", __func__);
  return ret;
}
