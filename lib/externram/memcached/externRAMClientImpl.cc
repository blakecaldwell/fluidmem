/*
 * Copyright 2015 Blake Caldwell, University of Colorado
 * Copyright 2015 Youngbin Im, University of Colorado
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, May 2015
 */

/*
 * externRAMClientImpl.cc
 *
 * This file implements a concrete class of externRAMClient that interoperates
 * with memcached
*/

#include "externRAMClientImpl.hh"
#include <dbg.h>
#include <stdio.h>
#include <cstdint>  // for uint types
#include <sys/user.h> /* for PAGE_SIZE */
#include <string>
#ifdef TIMING
#include <timingstats.h>
#endif
#include <userfault.h>

std::string locator;
memcached_pool_st * pool;
#ifdef THREADED_WRITE_TO_EXTERNRAM
memcached_pool_st * pool_write;
#endif
#ifdef THREADED_PREFETCH
memcached_pool_st * pool_multiread;
#endif

/*
 *** externRAMClient::create() ****
 * This is the Pointer to Implementation Pattern
 ********************************
 */
externRAMClient * externRAMClient::create(int type, char * config, uint64_t upid)
{
  log_trace_in("%s", __func__);
  char upidStr[100];
  char locatorStr[200];
  snprintf(upidStr, sizeof(upidStr), "%llu", upid);
  snprintf(locatorStr, sizeof(locatorStr), "%s --HASH-WITH-NAMESPACE --NAMESPACE=\"%s\"", config, upidStr);
  locator = locatorStr;
  log_debug("%s: locator : [%s] len: %d", __func__, locator.c_str(), locator.length());
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

  hashcodeStrings = (char**) malloc( MAX_MULTI_READ * sizeof(char*) );
  key_lengths = (size_t*) malloc( MAX_MULTI_READ  * sizeof(size_t) );
  for( int j=0; j<MAX_MULTI_READ; j++ )
  {
    key_lengths[j] = KEYSTR_LEN;
    hashcodeStrings[j] = (char*) malloc( KEYSTR_LEN+1 );
  }

  memcached_return_t rc;
  pool = memcached_pool(locator.c_str(), locator.length());
  memcached_st *memc = memcached_pool_pop(pool, false, &rc);
  if (rc != MEMCACHED_SUCCESS)
  {
    log_err("%s: memcached error, code: %ud, string: %s", __func__, rc, memcached_strerror(memc, rc));
  }
  myClient = memc;
#ifdef THREADED_WRITE_TO_EXTERNRAM
  pool_write = memcached_pool(locator.c_str(), locator.length());
  memcached_st *memc_write = memcached_pool_pop(pool_write, false, &rc);
  if (rc != MEMCACHED_SUCCESS)
  {
    log_err("%s: memcached error, code: %ud, string: %s", __func__, rc, memcached_strerror(memc_write, rc));
  }
  myClient_write = memc_write;
#endif
#ifdef THREADED_PREFETCH
  pool_multiread = memcached_pool(locator.c_str(), locator.length());
  memcached_st *memc_multiread = memcached_pool_pop(pool_multiread, false, &rc);
  if (rc != MEMCACHED_SUCCESS)
  {
    log_err("%s: memcached error, code: %ud, string: %s", __func__, rc, memcached_strerror(memc_multiread, rc));
  }
  myClient_multiread = memc_multiread;
#endif
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
  // Release the memc_ptr that was pulled from the pool
  memcached_pool_push(pool, myClient);
#ifdef THREADED_WRITE_TO_EXTERNRAM
  memcached_pool_push(pool, myClient_write);
#endif
#ifdef THREADED_PREFETCH
  memcached_pool_push(pool, myClient_multiread);
#endif

  // Destroy the pool.
  memcached_pool_destroy(pool);
#ifdef THREADED_WRITE_TO_EXTERNRAM
  memcached_pool_destroy(pool_write);
#endif
#ifdef THREADED_PREFETCH
  memcached_pool_destroy(pool_multiread);
#endif
  log_trace_out("%s", __func__);

  for( int j=0; j<MAX_MULTI_READ; j++ )
  {
    free(hashcodeStrings[j]);
  }
  free(hashcodeStrings);
  free(key_lengths);
}

/*
 *** externRAMClientImpl::write() ***
 *
 * write a key,value pair in memcached
 *
 @ hashcode: unique key
 @ data: buffer pointing to data to be stored in memcached
 @ size: length in bytes of the data to be stored
 -> returns: pointer of buffer to free
 **********************************************
 */
void* externRAMClientImpl::write(uint64_t hashcode, void **data, int size, int *err) {
  // only a single write is supported
  log_trace_in("%s", __func__);
  declare_timers();

  int retry = 1;
  memcached_st * client = myClient;
#ifdef THREADED_WRITE_TO_EXTERNRAM
  client = myClient_write;
#endif

  void * ret = NULL;
  *err = 0;

  /* write to memcached */
  char hashcodeStr[KEYSTR_LEN+1] = "";
  sprintf( hashcodeStr, "%lx", hashcode );

  start_timing_bucket(start, KVWRITE);
  while (retry--) {
    memcached_return_t rc = memcached_set(client, hashcodeStr, strlen(hashcodeStr), (char*) *data, size, 0, 0);
    *err = -1;
    switch (rc) {
      case MEMCACHED_SERVER_MEMORY_ALLOCATION_FAILURE:
        log_warn("%s: memcached cannot allocate more memory", __func__);
        ret = NULL;
        retry++;
        sleep(0.1);
        break;
      case MEMCACHED_SERVER_TEMPORARILY_DISABLED:
        /* allow server restart during testing */
        log_warn("%s: externRAMClientImpl::write() ERR: data may be lost. memcached server has failed", __func__);
        ret = NULL;
        retry++;
        sleep(1);
        break;
      case MEMCACHED_SUCCESS:
        *err = 0;
        ret = *data;
        break;
      default:
        log_err("%s: externRAMClientImpl::write() memcached error, code: %ud, string: %s", __func__, rc, memcached_strerror(client, rc));
        ret = NULL;
        break;
    }
  }
  stop_timing(start, end, KVWRITE);

  log_trace_out("%s", __func__);
  return ret;
}


/*
 *** externRAMClientImpl::read() ***
 *
 * read the data associated with a given key in memcached
 *
 @ hashcode: unique key
 @ recvBuf:
 -> returns: length of recvBuf
 **********************************************
 */
int externRAMClientImpl::read(uint64_t hashcode, void ** recvBuf) {
  log_trace_in("%s", __func__);
  size_t length = 0;
  uint32_t flags = 0;
  memcached_return_t error;
  char * buf = NULL;
  char hashcodeStr[20] = "";
  declare_timers();

  sprintf( hashcodeStr, "%lx", hashcode );
  start_timing_bucket(start, KVREAD);
  buf = memcached_get( myClient, hashcodeStr, strlen(hashcodeStr), &length, &flags, &error );
  stop_timing(start, end, KVREAD);

  switch (error) {
    case MEMCACHED_SUCCESS:
      start_timing_bucket(start, KVCOPY);
      memcpy(*recvBuf, buf, length);
      stop_timing(start, end, KVCOPY);
      break;
    case MEMCACHED_NOTFOUND:
      log_debug("%s: memcached returned not found, code: %u, string: %s", __func__, error, memcached_strerror(myClient, error));
      break;
    default:
      break;
  }

  if (buf != NULL)
    free(buf);
  log_trace_out("%s", __func__);
  return (int) length;
}

void externRAMClientImpl::buildHashStrings(uint64_t * hashcodes, int num_prefetch) {
  log_trace_in("%s", __func__);

  for( int j=0; j<num_prefetch; j++ )
  {
    sprintf( hashcodeStrings[j], "%lx", hashcodes[j] );
  }
  log_trace_out("%s", __func__);
  return;
}

/*
 *** externRAMClientImpl::MultiRead() ***
 *
 * read the data associated with one key in memcached and prefetch
 * additional keys
 *
 @ hashcodes: pointer to an array of unique keys
 @ num_prefetch: number of keys to prefetch with spatial locality
 @ recvBufs: pointer to an array of receive buffers
 @ lengths: pointer to an array of lengths to be recoreded by this function
 -> returns: void
 **********************************************
 */
int externRAMClientImpl::multiRead(uint64_t * hashcodes, int num_prefetch, void ** recvBufs, int * lengths) {
  log_trace_in("%s", __func__);

  // TODO: memcached does not support zerocopy because it allocates its own buffer
  //       in mget and we can't get that buffer from libexternram to libuserfault
  for( int i=0; i<num_prefetch; i++ )
  {
    recvBufs[i] = malloc(PAGE_SIZE);
  }

  multiRead_top(hashcodes, num_prefetch, recvBufs, lengths);
  multiRead_bottom(hashcodes, num_prefetch, recvBufs, lengths);
  log_trace_out("%s", __func__);
  return 0;
}

#ifdef ASYNREAD
void externRAMClientImpl::read_top(uint64_t key, void ** value) {
  log_trace_in("%s", __func__);
  multiRead_top(&key, 1, (void**) NULL, NULL);
  log_trace_out("%s", __func__);
  return;
}

int externRAMClientImpl::read_bottom(uint64_t hashcode, void ** recvBuf) {
  log_trace_in("%s", __func__);
  int length;
  multiRead_bottom(&hashcode, 1, recvBuf, &length);
  log_trace_out("%s", __func__);
  return length;
}

void externRAMClientImpl::multiRead_top(uint64_t * hashcodes, int num_prefetch, void ** recvBufs, int * lengths) {
  log_trace_in("%s", __func__);
  memcached_return_t error;
  uint32_t flags;
  memcached_st * client = myClient;
#ifdef THREADED_PREFETCH
  client = myClient_multiread;
#endif

  buildHashStrings( hashcodes, num_prefetch );
  error = memcached_mget( client, hashcodeStrings, key_lengths, num_prefetch );
  if (error != MEMCACHED_SUCCESS)
  {
    log_err("%s: memcached error, code: %u, string: %s", __func__, error, memcached_strerror(client, error));
  }

  log_trace_out("%s", __func__);
  return;
}

int externRAMClientImpl::multiRead_bottom(uint64_t * hashcodes, int num_prefetch, void ** recvBufs, int * lengths) {
  log_trace_in("%s", __func__);
  size_t key_length[num_prefetch];
  memcached_return_t error;
  memcached_return_t rc;
  uint32_t flags;
  char return_key[MEMCACHED_MAX_KEY];
  size_t return_key_length;
  char *return_value;
  size_t return_value_length;
  memcached_st * client = myClient;
  unsigned int i = 0;
#ifdef THREADED_PREFETCH
  client = myClient_multiread;
#endif
  declare_timers();

  memset(lengths, 0, sizeof(int) * num_prefetch);
  while ((return_value = memcached_fetch( client, return_key, &return_key_length,
                                    &return_value_length, &flags, &rc)))
  {
    // return_value != NULL
    if(( rc == MEMCACHED_SUCCESS ) && ( return_value != NULL ))
    {
      if( i == num_prefetch ) {
        log_err("%s: memcached returned too many keys", __func__);
        break;
      }

      if( memcmp(return_key,hashcodeStrings[i],return_key_length)!=0 )
      {
        log_err("memcached could not retrieve the value for key %lx, expected %s, but got %s", hashcodes[i], hashcodeStrings[i], return_key);
      }
      lengths[i] = return_value_length;
      start_timing_bucket(start, KVCOPY);
      memcpy(recvBufs[i], return_value, lengths[i]);
      stop_timing(start, end, KVCOPY);
    }
    else if( rc != MEMCACHED_SUCCESS )
    {
      log_err("%s: memcached error, code: %u, string: %s", __func__, error, memcached_strerror(client, rc));
    }
    if ( return_value != NULL ) {
      free( return_value );
    }
    i++;
  }

  log_trace_out("%s", __func__);
  return 0;
}
#endif

/*
 *** externRAMClientImpl::MultiWrite() ***
 *
 * write the data associated with multiple keys in memcached seqeuntially
 *
 @ hashcodes: pointer to an array of unique keys
 @ num_write: number of keys to write
 @ data: pointer to an array of buffers to write
 @ lengths: pointer to an array of lengths to write
 -> returns: should free be called
 **********************************************
 */
bool externRAMClientImpl::multiWrite(uint64_t * hashcodes, int num_write, void ** data, int * lengths, int *err) {
  log_trace_in("%s", __func__);
  bool should_free = false;
  void * ret_addr;
  int read_err;
  *err = 0;

  int ret = 0;
  for( int i=0; i<num_write; i++ )
  {
    ret_addr = write( hashcodes[i], &data[i], lengths[i], &read_err );
    if( read_err < 0 )
      *err = read_err;
    if( ret_addr != NULL ) {
      should_free = true;
    }
  }
  log_trace_out("%s", __func__);
  return should_free;
}

/*
 *** externRAMClientImpl::MultiReadTest() ***
 *
 * test the multiRead function showing how it should be used
 *
 -> returns: void
 **********************************************
 */
void externRAMClientImpl::multiReadTest()
{
  log_trace_in("%s", __func__);
  uint64_t keys[] = { 123,456,7890 };
  char values[3][50] = { "dddddddddddddddddddddd", "fffffffffffffffffff", "gggggggggggggggg" };
  char * buf[3];
  int length[3];
  int err;
  for( int i=0; i<3; i++ )
  {
    write( keys[i], (void **) &values[i], strlen(values[i]), &err );
  }

  multiRead( keys, 3, (void**) buf, length );
  for( int j=0; j<3; j++ )
  {
    log_info("%d key %ld value %s legnth %d", j, keys[j], buf[j], length[j] );
    free(buf[j]); // buffer should be freed at the caller after using it
  }

  remove( keys[1] );
  multiRead( keys, 3, (void**) buf, length );
  for( int j=0; j<3; j++ )
  {
    log_info("%d key %ld value %s legnth %d", j, keys[j], buf[j], length[j] );
    if( buf[j] )
      free(buf[j]); // buffer should be freed at the caller after using it
  }
  log_trace_out("%s", __func__);
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
int externRAMClientImpl::remove(uint64_t hashcode) {
  log_trace_in("%s", __func__);

  int ret = 0;
  int retry = 1;
  memcached_st * client = myClient;

  /* write to memcached */
  char hashcodeStr[20] = "";
  sprintf( hashcodeStr, "%lx", hashcode );
  while (retry--) {
    memcached_return_t rc = memcached_delete(client, hashcodeStr, strlen(hashcodeStr), 0);
    switch (rc) {
      case MEMCACHED_SERVER_MEMORY_ALLOCATION_FAILURE:
        log_warn("externRAMClientImpl::write() WARN: memcached cannot allocate more memory");
        retry++;
        sleep(0.1);
        break;
      case MEMCACHED_SERVER_TEMPORARILY_DISABLED:
        /* allow server restart during testing */
        log_warn("externRAMClientImpl::write() ERR: data may be lost. memcached server has failed");
        retry++;
        sleep(1);
        break;
      case MEMCACHED_SUCCESS:
        ret = 1;
        break;
      default:
        log_err("externRAMClientImpl::write() memcached error, code: %ud, string: %s", rc, memcached_strerror(client, rc));
        break;
    }
  }

  log_trace_out("%s", __func__);
}
