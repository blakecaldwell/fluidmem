/*
 * Copyright 2016 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, Oct 2016
 */

/* FluidMem includes */
#include <affinity.h>  /* should be the first to be included */
#ifdef MONITORSTATS
#include <monitorstats.h>
#endif
#ifdef TIMING
#include <timingstats.h>
#endif
#include "userfault.h"
#include "common.h"
#include <dbg.h>

/* cstdlib includes */
#include <sys/user.h>
#include <stdbool.h>
#include <stdint.h>      /* for uint64_t */
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/syscall.h> /* for __NR_userfaultfd */
#include <unistd.h>      /* for syscall */
#include <stdio.h>
#include <fcntl.h>       /* for O_CLOEXEC */
#include <errno.h>
#include <string.h>      /* for __func__ */
#include <fcntl.h>
#include <stdlib.h>
#include <linux/un.h>
#include <bits/socket.h>

#include <LRUBufferWrapper.h>
#include <PageCacheWrapper.h>
#include "../lrubuffer/c_cache_node.h"
#include "zookeeper_upid.h"
#include <upid.h>
#include <threaded_io.h>
#include <buffer_allocator_array.h>

#ifdef MONITORSTATS
#include <monitorstats.h>
#endif
#if defined(THREADED_WRITE_TO_EXTERNRAM) || defined(THREADED_PREFETCH)
#include <threaded_io.h>
#endif

char * config;
struct LRUBuffer *lru;
#ifdef PAGECACHE
struct PageCache * pageCache;
extern int prefetch_size;
#endif
#define MAX_PENDING 100

pthread_mutex_t lru_lock;
#ifdef PAGECACHE
pthread_mutex_t pagecache_lock;
#endif

#ifdef THREADED_REINIT
extern page_buffer_info* buf_readpage;
extern page_buffer_info* buf_evictpage;
#endif

#ifdef THREADED_WRITE_TO_EXTERNRAM
void *write_into_externram_thread(void * tmp) {
  log_trace_in("%s", __func__);
  setThreadCPUAffinity(CPU_FOR_WRITE_THREAD, "write_thread", TID());

  declare_timers();

  bool flushRemaining = false;
  while(true)
  {
    uint64_t keys[MAX_MULTI_WRITE];
    void * bufs[MAX_MULTI_WRITE];
    int lengths[MAX_MULTI_WRITE];
    bool toWrite = false;
    int size = 0;
    int numWrite = 0;
    int ufd = 0;
    bool free_values = false;

    log_lock("%s: locking list_lock", __func__);
    pthread_mutex_lock(&list_lock);
    log_lock("%s: locked list_lock", __func__);

    size = get_write_list_size();

    log_lock("%s: locking flush_write_needed_lock", __func__);
    pthread_mutex_lock(&flush_write_needed_lock);
    log_lock("%s: locked flush_write_needed_lock", __func__);
    if( (size>0) &&
        ( (size>=WRITE_BATCH_SIZE) ||
          (isUfhandlerWaiting) ||
          (flushRemaining) ||
          (flushWriteListNeeded) ) )
    {
      log_lock("%s: unlocking flush_write_needed_lock", __func__);
      pthread_mutex_unlock(&flush_write_needed_lock);
      log_lock("%s: unlocked flush_write_needed_lock", __func__);

      write_info *current, *tmp;
      int isFirst=1;
      HASH_ITER( hh2, write_list, current, tmp )
      {
        if( isFirst==1 )
        {
          ufd = current->key.ufd;
          isFirst = 0;
        }
        if( current->key.ufd == ufd && numWrite<WRITE_BATCH_SIZE ) // we can do multiwrite for only one ufd
        {
          keys[numWrite] = *((uint64_t*)current->key.pageaddr);
          bufs[numWrite] = current->page;
          lengths[numWrite] = PAGE_SIZE;
          numWrite++;

          // mark page as in_flight
          current->in_flight = true;

          log_debug(
            "%s: writing the page %p stored at %p will be started by write_thread. write_list_size : %d",
            __func__, *((uint64_t*)current->key.pageaddr), current->page, size );
        }
      }
      // always have to write to externram, but we can drop list_lock
      toWrite = true;

      if( numWrite < size ) {
        flushRemaining = true;
      }
      else {
        flushRemaining = false;
        // we can wait until writes have completed before posting to flushed_write_sem
      }
      isWriterWaiting = false;
    }
    else {
      if (flushWriteListNeeded) {
        sem_post(&flushed_write_sem);
        log_lock("%s: sem_posted flushed_write_sem", __func__);
      }
      log_lock("%s: unlocking flush_write_needed_lock", __func__);
      pthread_mutex_unlock(&flush_write_needed_lock);
      log_lock("%s: unlocked flush_write_needed_lock", __func__);

      isWriterWaiting = true;
    }
    log_lock("%s: unlocking list_lock", __func__);
    pthread_mutex_unlock(&list_lock);
    log_lock("%s: unlocked list_lock", __func__);

    if(toWrite)
    {
      bool waiting = false;
      int j=0, k=0;
      struct externRAMClient *client = get_client_by_fd(ufd);
      if (client) {
        start_timing_bucket(start, WRITE_PAGES);
        free_values = writePages(client, keys, numWrite, (void**) bufs, lengths);
        stop_timing(start, end, WRITE_PAGES);

#ifdef DEBUG
        for( j=0 ; j<numWrite; j++ ) {
          if (lengths[j] < 0)
            log_debug("%s: failed to write %p", __func__, bufs[j]);
        }
#endif
      }
      else {
        log_debug("%s: skipping writing %d pages belonging to invalid fd %d", __func__, numWrite, ufd);
        free_values = true;
      }

      log_lock("%s: locking list_lock", __func__);
      pthread_mutex_lock(&list_lock);
      log_lock("%s: locked list_lock", __func__);
      for( j=0 ; j<numWrite; j++ ) {
        log_debug("%s: removing the key %p from the write list", __func__, keys[j]);
        del_write_info( ufd, keys[j] );
      }
      waiting = isUfhandlerWaiting;
      log_lock("%s: unlocking list_lock", __func__);
      pthread_mutex_unlock(&list_lock);
      log_lock("%s: unlocked list_lock", __func__);

      // If write of existing pages to be flushed was completed, then
      // we can post to flushed_write_sem. We assume that numPages
      // were written to externram and removed from the write_list
      // It's possible that more pages will be added to the list on each
      // iteration, so this may not work very well for very active ufds.
      // However flushed_write_sem will keep purgeDeadUpids blocked
      if (!flushRemaining) {
        sem_post(&flushed_write_sem);
        log_lock("%s: sem_posted flushed_write_sem", __func__);
      }

      if (free_values) {
        for( k=0 ; k<numWrite; k++ )
        {
          if (bufs[k] != NULL) {
            // libexternram says it is done with the buffer
#ifdef THREADED_REINIT
            int ret = return_free_page(buf_evictpage, bufs[k]);
#else
            int ret = munmap(bufs[k], PAGE_SIZE);
#endif
            if (ret < 0) {
              log_err("%s: munmap to %p", __func__, bufs[k]);
            }
            else {
              log_debug("%s: munmap to %p", __func__, bufs[k]);
            }
          }
        }
      }

      if( waiting )
      {
        sem_post(&ufhandler_sem);
        log_lock("%s: sem_posted ufhandler_sem", __func__);
      }
    }
    else if ( !flushRemaining )
    {
      // write thread's work is done for now
      log_lock("%s: sem_waiting writer_sem", __func__);
      sem_wait(&writer_sem);
      log_lock("%s: sem_waited writer_sem", __func__);
    }
  }
  log_trace_out("%s", __func__);
}
#endif
#ifdef THREADED_PREFETCH
void *prefetch_thread(void * tmp) {
  log_trace_in("%s", __func__);
  setThreadCPUAffinity(CPU_FOR_PREFETCH_THREAD, "prefetch_thread", TID());

  declare_timers();

  while(true)
  {
    uint64_t keys[MAX_MULTI_READ];
    void * bufs[MAX_MULTI_READ];
    int lengths[MAX_MULTI_READ];
    bool toPrefetch = false;
    int size = 0;
    int numPrefetch = 0;
    int ufd = 0;
    int i = 0;

    log_lock("%s: locking list_lock", __func__);
    pthread_mutex_lock(&list_lock);
    log_lock("%s: locked list_lock", __func__);
    size = get_prefetch_list_size();
    if( size>0 )
    {
      prefetch_info *current, *tmp;
      int isFirst=1;
      HASH_ITER( hh3, prefetch_list, current, tmp )
      {
        if( isFirst==1 )
        {
          ufd = current->key.ufd;
          isFirst = 0;
        }
        if( current->key.ufd == ufd && numPrefetch<MAX_MULTI_READ ) // we can do multiread for only one ufd
        {
          keys[numPrefetch] = *((uint64_t*)current->key.pageaddr);
          numPrefetch++;
          log_debug("%s: prepared info to fetch page %p. prefetch_list_size : %d", __func__, *((uint64_t*)current->key.pageaddr), size );
        }
      }
      toPrefetch = true;
      isPrefetcherWaiting = false;
    }
    else {
      // nothing on prefetch_list
      isPrefetcherWaiting = true;
    }

    log_lock("%s: unlocking list_lock", __func__);
    pthread_mutex_unlock(&list_lock);
    log_lock("%s: unlocked list_lock", __func__);
    if(toPrefetch)
    {
      log_debug("%s: starting %d prefetches with key %lx", __func__, numPrefetch, keys[0]);

      bool waiting = false;
      int i=0;
      struct externRAMClient *client = get_client_by_fd(ufd);
      if (client) {

#ifdef ASYNREAD
        log_lock("%s: locking read_lock", __func__);
        pthread_mutex_lock(&read_lock);
        log_lock("%s: locked read_lock", __func__);
#endif

        start_timing_bucket(start, READ_PAGES);
        readPages(client, keys, numPrefetch, (void**) bufs, lengths );
        stop_timing(start, end, READ_PAGES);

#ifdef ASYNREAD
        log_lock("%s: unlocking read_lock", __func__);
        pthread_mutex_unlock(&read_lock);
        log_lock("%s: unlocked read_lock", __func__);
#endif
      }
      else {
        log_warn("%s: skipping prefetching for invalid fd %d", ufd);
        sem_wait(&prefetcher_sem);
        continue;
      }
#ifdef DEBUG
      for( i=0; i<numPrefetch; i++ ) {
        if ((lengths[i] != PAGE_SIZE) || (bufs[i] == NULL)) {
          log_err("%s: prefetch failed for key %lx with length %lx, index %u", __func__, keys[i], lengths[i], i);
        }
      }
#endif
      log_lock("%s: locking pagecache_lock", __func__);
      pthread_mutex_lock(&pagecache_lock);
      log_lock("%s: locked pagecache_lock", __func__);

      start_timing_bucket(start, STORE_PAGES_IN_PAGE_CACHE);
      storePagesInPageCache( pageCache, &keys[0], ufd, numPrefetch, (char**) &bufs[0], &lengths[0]);
      stop_timing(start, end, STORE_PAGES_IN_PAGE_CACHE);

      log_lock("%s: unlocking pagecache_lock", __func__);
      pthread_mutex_unlock(&pagecache_lock);
      log_lock("%s: unlocked pagecache_lock", __func__);

      log_lock("%s: locking list_lock", __func__);
      pthread_mutex_lock(&list_lock);
      log_lock("%s: locked list_lock", __func__);

      for( i=0; i<numPrefetch; i++ )
      {
        del_prefetch_info( ufd, keys[i] );
        log_debug("%s: prefetching the page %p completed by prefetch_thread.", __func__, keys[i]);
      }
      waiting = isUfhandlerWaiting;

      log_lock("%s: unlocking list_lock", __func__);
      pthread_mutex_unlock(&list_lock);
      log_lock("%s: unlocked list_lock", __func__);
      if( waiting )
      {
        sem_post(&ufhandler_sem);
        log_lock("%s: sem_posted ufhandler_sem", __func__);
      }
    }
    else
    {
      log_lock("%s: sem_waiting on prefetcher_sem", __func__);
      sem_wait(&prefetcher_sem);
      log_lock("%s: sem_waited on prefetcher_sem", __func__);
    }
  }
  log_trace_out("%s", __func__);
}
#endif

int evict_if_needed(int ufd, void * dst, int page_type) {
  log_trace_in("%s", __func__);
  declare_timers();

  int ret = 0, ret2 = 0;
  uint64_t key;

#ifdef CACHE
  log_lock("%s: locking lru_lock", __func__);
  pthread_mutex_lock(&lru_lock);
  log_lock("%s: locked lru_lock", __func__);

  start_timing_bucket(start, INSERT_LRU_CACHE_NODE);
  struct c_cache_node evict_node = insertCacheNodeAndEvict(lru, (uint64_t)dst, ufd);
  stop_timing(start, end, INSERT_LRU_CACHE_NODE);

  log_lock("%s: unlocking lru_lock", __func__);
  pthread_mutex_unlock(&lru_lock);
  log_lock("%s: unlocked lru_lock", __func__);

  if (evict_node.hashcode != 0) {
    key = evict_node.hashcode & (uint64_t)(PAGE_MASK);

    switch(page_type) {
      case COPY_PAGE:
#ifdef TIMING
        strcpy(timing_label, "COPY_EVICT");
        bucket_index=1;
#endif
        break;
      case ZERO_PAGE:
#ifdef TIMING
        strcpy(timing_label, "ZERO_EVICT");
        bucket_index=3;
#endif
        break;
      case MOVE_PAGE:
#ifdef TIMING
        strcpy(timing_label, "MOVE_EVICT");
        bucket_index=5;
#endif
      case ASYN_PAGE:
#ifdef TIMING
        strcpy(timing_label, "ASYN_EVICT");
        bucket_index=6;
#endif
        break;
    }

    start_timing_bucket(start, EVICT_TO_EXTERNRAM);
    ret2 = evict_to_externram(evict_node.ufd, (void*)(uintptr_t)key);
    stop_timing(start, end, EVICT_TO_EXTERNRAM);

    if (ret2 == 0) {
      log_debug("%s: eviction of page %p succeeded", __func__,  (void*)(uintptr_t)key);
    }
    else if (ret2 == 1) {
#ifdef MONITORSTATS
      StatsIncrWriteSkippedInvalid_notlocked();
#endif
      log_debug("%s: eviction of page %p skipped", __func__, (void*)(uintptr_t)key);

      // return ufd of skipped page to caller
      ret = evict_node.ufd;
    }
    else if (ret2 == 2) {
#ifdef MONITORSTATS
      StatsIncrWriteSkippedZero_notlocked();
#endif
      log_debug("%s: tried evicting zeropage %p from ufd %d, putting it back on lrubuffer", __func__, (void*)(uintptr_t)key, evict_node.ufd);
      log_lock("%s: locking lru_lock", __func__);
      pthread_mutex_lock(&lru_lock);
      log_lock("%s: locked lru_lock", __func__);

      start_timing_bucket(start, INSERT_LRU_CACHE_NODE);
      insertCacheNode(lru, key, evict_node.ufd);
      stop_timing(start, end, INSERT_LRU_CACHE_NODE);

      log_lock("%s: unlocking lru_lock", __func__);
      pthread_mutex_unlock(&lru_lock);
      log_lock("%s: unlocked lru_lock", __func__);
    }
    else {
      log_err("%s: eviction of page %p failed", __func__, (void*)(uintptr_t)key);
      ret = ret2;
    }
  }

#endif

  log_trace_out("%s", __func__);
  return ret;
}

int place_data_page(int ufd, void * dst, void * src) {
  log_trace_in("%s", __func__);
  declare_timers();

  int ret = 0, rc = 0;
  struct uffdio_copy copy_struct;
  copy_struct.dst = (uint64_t)(uintptr_t)dst;
  copy_struct.src = (uint64_t)(uintptr_t)src;
  copy_struct.len = PAGE_SIZE;
  copy_struct.mode = 0;
  copy_struct.copy = 0;

#ifdef TIMING
  strcpy(timing_label, "COPY");
  bucket_index=0;
#endif

  start_timing_bucket(start, UFFD_COPY);
  rc = ioctl(ufd, UFFDIO_COPY, &copy_struct);
  stop_timing(start, end, UFFD_COPY);

  if (rc) {
    if (copy_struct.copy != PAGE_SIZE) {
    ret = copy_struct.copy;
      switch(copy_struct.copy) {
        case EBUSY:
        case ENOENT:
        case ESRCH:
          log_warn("%s: src: %p, dst: %p, ufd: %d", __func__, src, dst, ufd);
          break;
        default:
          log_err("%s: src: %p, dst: %p, ufd: %d, size: %Lu", __func__, src, dst, ufd, copy_struct.copy);
          break;
      }
    }
  }
  else {
    log_debug("%s: src: %p, dst %p", __func__, src, dst);
  }

#ifndef ASYNREAD
  ret = evict_if_needed(ufd, dst, COPY_PAGE);
#endif

  // ret passed through in case this ufd needs to be removed from polling

#ifdef MONITORSTATS
  StatsIncrPlacedPage_notlocked();
#endif

  log_trace_out("%s", __func__);
  return ret;
}

int place_zero_page(int ufd, void * dst) {
  log_trace_in("%s", __func__);
  declare_timers();

  int ret = 0, rc = 0;

  struct uffdio_zeropage zero_struct;
  zero_struct.range.start = (uint64_t)(uintptr_t)dst & (uint64_t)(PAGE_MASK);
  zero_struct.range.len = PAGE_SIZE;
  zero_struct.mode = 0;

#ifdef TIMING
  strcpy(timing_label, "ZERO");
  bucket_index=2;
#endif

  start_timing_bucket(start, UFFD_ZEROPAGE);
  rc = ioctl(ufd, UFFDIO_ZEROPAGE, &zero_struct);
  stop_timing(start, end, UFFD_ZEROPAGE);

  if (rc) {
    ret = errno;
    switch(errno) {
      case EBUSY:
      case ENOENT:
      case EINVAL:
      case ESRCH:
        log_warn("%s: dst: %p, ufd: %d", __func__, dst, ufd);
        break;
      default:
        log_err("%s: dst: %p, ufd: %d", __func__, dst, ufd);
        break;
    }
  }
#ifdef DEBUG
  else {
    log_debug("%s: dst %p", __func__, dst);
  }
#endif

#ifndef ASYNREAD
  ret = evict_if_needed(ufd, dst, ZERO_PAGE);
#endif

  // ret passed through in case this ufd needs to be removed from polling

  log_trace_out("%s", __func__);
  return ret;
}

int move_page(int ufd, void * dst, void * src) {
  log_trace_in("%s", __func__);
  declare_timers();

  struct uffdio_remap move_struct;
  int ret = 0, rc = 0;

  move_struct.dst = (uint64_t)(uintptr_t)dst & (uint64_t)(PAGE_MASK);
  move_struct.src = (uint64_t)(uintptr_t)src;
  move_struct.len = PAGE_SIZE;
  move_struct.mode = 0;
  move_struct.mode |= UFFDIO_REMAP_MODE_DIRECTION_IN;

#ifdef TIMING
  strcpy(timing_label, "MOVE");
  bucket_index=4;
#endif

  start_timing_bucket(start, UFFD_REMAP);
  rc = ioctl(ufd, UFFDIO_REMAP, &move_struct);
  stop_timing(start, end, UFFD_REMAP);

  if (rc) {
    ret = errno;
    switch(errno) {
      case EBUSY:
        log_info("%s: src: %p, dst: %p, ufd: %d", __func__, src, dst, ufd);
        break;
      case ENOENT:
      case EINVAL:
      case ESRCH:
        log_warn("%s: src: %p, dst: %p, ufd: %d", __func__, src, dst, ufd);
        break;
      default:
        log_err("%s: src: %p, dst: %p, ufd: %d", __func__, src, dst, ufd);
        break;
    }
  }
#ifdef DEBUG
  else {
    log_debug("%s: src: %p, dst %p", __func__, src, dst);
  }
#endif

#ifndef ASYNREAD
  ret = evict_if_needed(ufd, dst, MOVE_PAGE);
#endif

  // ret passed through in case this ufd needs to be removed from polling

  log_trace_out("%s", __func__);
  return ret;
}

int evict_page(int ufd, void * dst, void * src) {
  log_trace_in("%s", __func__);

  struct uffdio_remap move_struct;
  int ret = 0, rc = 0;
  declare_timers();

  move_struct.src = (uint64_t)(uintptr_t)src & (uint64_t)(PAGE_MASK);
  move_struct.dst = (uint64_t)(uintptr_t)dst;
  move_struct.len = PAGE_SIZE;
  move_struct.mode = UFFDIO_REMAP_MODE_DONTWAKE | UFFDIO_REMAP_MODE_ALLOW_SRC_HOLES;

  start_timing_bucket(start, UFFD_REMAP);
  rc = ioctl(ufd, UFFDIO_REMAP, &move_struct);
  stop_timing(start, end, UFFD_REMAP);

  if (rc) {
    if (errno != PAGE_SIZE) {
      ret = errno;
      switch(errno) {
        case EBUSY:
          // tried evicting zeropage
          log_debug("%s: src: %p, dst %p, ufd: %d", __func__, src, dst, ufd);
          break;
        case EEXIST:
          log_warn("%s: src: %p, dst %p, ufd: %d", __func__, src, dst, ufd);
          break;
        case EINVAL:
        case ESRCH:
          // tried evicting page from invalid userfault region (ufd closed or pid dead)
          log_debug("%s: src: %p, dst %p, ufd: %d", __func__, src, dst, ufd);
          break;
        default:
          log_err("%s: src: %p, dst: %p, ufd: %d", __func__, src, dst, ufd);
          break;
      }
    }
  }
#ifdef DEBUG
  else {
    log_debug("%s: src: %p, dst: %p, ufd: %d", __func__, src, dst, ufd);
  }
#endif

  log_trace_out("%s", __func__);
  return ret;
}

int ack_userfault(int ufd, void *addr, size_t len)
{
  log_trace_in("%s", __func__);

  struct uffdio_range range_struct;

  range_struct.start = (uint64_t)(uintptr_t)addr;
  range_struct.len = (uint64_t)len;

  errno = 0;
  int rc = 0;
  declare_timers();

  rc = ioctl(ufd, UFFDIO_WAKE, &range_struct);

  if (rc) {
      if (errno == ENOENT) {
          /* Kernel said it wasn't waiting - one case where this can
           * happen is where two threads triggered the userfault
           * and we receive the page and ack it just after we received
           * the 2nd request and that ends up deciding it should ack it
           * We could optimise it out, but it's rare.
           */
          log_info("%s: %p/%zx ENOENT", __func__, addr, len);
          return 0;
      }
      log_err("%s: failed to notify kernel for %p/%zx", __func__,
              addr, len);
      return -errno;
  }

  log_trace_out("%s", __func__);
  return 0;
}

int create_buffers()
{
  log_trace_in("%s", __func__);

  int ret = 0;

  if (pthread_mutex_init(&zh_lock, NULL) != 0)
  {
    log_err("%s: zh lock init failed", __func__);
    ret = -1;
  }

  if (pthread_mutex_init(&fdUpidMap_lock, NULL) != 0)
  {
    log_err("%s: fd map lock init failed", __func__);
    ret = -1;
  }

  if (pthread_mutex_init(&lru_lock, NULL) != 0)
  {
    log_err("%s: lru lock init failed", __func__);
    ret = -1;
  }
  lru = newLRUBuffer();
  if (!lru) {
    log_err("%s: creating LRUBuffer", __func__);
    ret = -1;
  }

#ifdef THREADED_WRITE_TO_EXTERNRAM
  pthread_mutex_init(&flush_write_needed_lock, NULL);
  sem_init(&flushed_write_sem, 0, 0);
#endif

#ifdef PAGECACHE
  if (pthread_mutex_init(&pagecache_lock, NULL) != 0)
  {
    log_err("%s: pagecache lock init failed", __func__);
    ret = -1;
  }
  pageCache = newPageCache(lru);
  if (!pageCache) {
    log_err("%s: creating PageCache", __func__);
    ret = -1;
  }
#endif

#ifdef THREADED_PREFETCH
#ifdef ASYNREAD
  if (pthread_mutex_init(&read_lock, NULL) != 0)
  {
    log_err("%s: read lock init failed", __func__);
    ret = -1;
  }
#endif
#endif

  read_tmp_page = get_local_tmp_page();
  if (!read_tmp_page) {
    log_err("failed to get evict tmp page");
    ret = -1;
  }

  log_trace_out("%s", __func__);

  return ret;
}

/* evict_to_externram_multi is called with lru_lock held */
int evict_to_externram_multi(int size)
{
  log_trace_in("%s", __func__);

  uint64_t key;
  int cnt = 0; // number of actually evicted pages
  struct c_cache_node * node_list;

  int num_to_evict = popNLRU(lru, size, &node_list);
  if (size > num_to_evict) {
    log_warn("%s: the LRUBuffer has %d pages, less than the number of pages to be evicted (%d).",
            __func__, num_to_evict, size);
  }

  int i;
  for( i=0; i < size; i++)
  {
    key = node_list[i].hashcode & (uint64_t)(PAGE_MASK);

    int ret = evict_to_externram(node_list[i].ufd, (void*)(uintptr_t)key);
    if (ret == 0) {
      log_debug("%s: eviction of page %p succeeded", __func__, (void*)(uintptr_t)key);
      cnt++;
    }
    else if (ret == 1) {
#ifdef MONITORSTATS
      StatsIncrWriteSkippedInvalid_notlocked();
#endif
      log_debug("%s: eviction of page %p skipped", __func__, (void*)(uintptr_t)key);
    }
    else if (ret == 2) {
      log_debug("%s: eviction of page %p delayed", __func__, (void*)(uintptr_t)key);
    }
    else {
      log_err("%s: eviction of page %p failed.", __func__, (void*)(uintptr_t)key);
    }
  }

  free(node_list);
  log_trace_out("%s", __func__);
  return cnt;
}

int resizeLRUBuffer(int size)
{
  log_trace_in("%s", __func__);

  /* Take lru_lock and hold until resize operation is finished,
   * which includes calls to evict_to_externram_multi()
   */

  log_lock("%s: locking lru_lock", __func__);
  pthread_mutex_lock(&lru_lock);
  log_lock("%s: locked lru_lock", __func__);

  int lru_size = getLRUBufferSize(lru);

  if (size > lru_size) {
    setLRUBufferSize(lru,size);

    log_lock("%s: unlocking lru_lock", __func__);
    pthread_mutex_unlock(&lru_lock);
    log_lock("%s: unlocked lru_lock", __func__);
  }
  else
  {
    int numToEvict = lru_size - size;

    // evict_to_externram_multi must evict numToEvict, so not allowed to skip pages
    int ret = evict_to_externram_multi(numToEvict);

    setLRUBufferSize(lru,size);

    log_lock("%s: unlocking lru_lock", __func__);
    pthread_mutex_unlock(&lru_lock);
    log_lock("%s: unlocked lru_lock", __func__);

    // print log message outside of lock scope
    if(ret != numToEvict) {
      log_warn(
               "%s: failed to evict all pages during resizing. attempted=%d, evicted=%d",
              __func__, numToEvict, ret);
    }
  }

  log_trace_out("%s", __func__);
}

/*
 * evict_to_externram store
 * This function will evict the page at pageaddr from the userfault
 * region described by ufd and the write the page to externram
 *
 * lru_lock may or may not be held by caller
 */
int evict_to_externram(int ufd, void * pageaddr) {
  log_trace_in("%s", __func__);

  declare_timers();
  int ret = -1;
  void *write_ret = NULL;
  int free_ret = -1;
  int retry = 5;
  bool skip_clean = false;
  void *evict_tmp_page;
  void **evict_tmp_page_ptr;

  // this page contains data (non zero byte), evict it
  while (retry > 0) {
#ifdef THREADED_REINIT
    evict_tmp_page = get_tmp_page(buf_evictpage);
#else
    evict_tmp_page = get_local_tmp_page();
#endif
    evict_tmp_page_ptr = &evict_tmp_page;

    if (!evict_tmp_page) {
      log_err("failed to get evict tmp page");
      return -1;
    }

    ret = evict_page(ufd, evict_tmp_page, (void *)pageaddr);
    if (ret != 0) {
      switch (ret) {
        case EBUSY:
          // tried evicting zeropage. delay eviction
          log_debug("%s: delaying page, rc: %d, pageaddr: %p", __func__, ret, pageaddr);
          ret = 2;
          retry = 0;
          break;
        case EAGAIN:
          log_warn("%s: retrying page, rc: %d, pageaddr: %p", __func__, ret, pageaddr);
          ret = 1;
          retry--;
          break;
        case ENOENT:
          log_recoverable_err("%s: retrying page, rc: %d, pageaddr: %p", __func__, ret, pageaddr);
          ret = -1;
          retry--;
          break;
        case EEXIST:
          log_warn("%s: retrying page, rc: %d, pageaddr: %p", __func__, ret, pageaddr);
          ret = -1;
          retry--;
          break;
        case EINVAL:
        case ESRCH:
          // tried evicting page from invalid userfault region (ufd closed or pid dead)
          log_debug("%s: skipping page for invalid fd %d, rc: %d, pageaddr: %p", __func__, ufd, ret, pageaddr);
          ret = 1;
          retry = 0;
          break;
        default:
          log_err("%s: rc: %d, pageaddr: %p", __func__,  ret, pageaddr);
          ret = -1;
          retry = 0;
          break;
      }
      // If retrying, need to free page. If done, page will be freed before returning
      if (retry > 0) {
        // fell through and about to retry. must munmap old evict_tmp_page
#ifdef THREADED_REINIT
        free_ret = return_free_page(buf_evictpage, evict_tmp_page);
#else
        free_ret = munmap(evict_tmp_page,PAGE_SIZE);
#endif
        if (free_ret < 0) {
          log_err("%s: munmap to %p", __func__, evict_tmp_page);
        }
        else {
          log_debug("%s: munmap to %p", __func__, evict_tmp_page);
        }
      }
    }
    // if ret is 0, then we are all good
    else
      retry = 0;
  }

  // check to make sure page was evicted
  if (ret == 0) {
    // Always increase evicted stat even if EVICT fails
#ifdef MONITORSTATS
    StatsIncrPageEvicted_notlocked();
#endif
#ifdef PAGECACHE_ZEROPAGE_OPTIMIZATION
    int cmp = -1;
    start_timing_bucket(start, ZEROPAGE_COMPARE);
    cmp = memcmp((void*)zeroPage, evict_tmp_page, PAGE_SIZE);
    stop_timing(start, end, ZEROPAGE_COMPARE);

    if (cmp ==0 )
    {
#ifdef MONITORSTATS
      StatsIncrWriteAvoided_notlocked();
#endif
#ifdef PAGECACHE
      start_timing_bucket(start, UPDATE_PAGE_CACHE);
      updatePageCacheAfterSkippedWrite(pageCache, ufd, (uint64_t)(uintptr_t)pageaddr);
      stop_timing(start, end, UPDATE_PAGE_CACHE);
#endif
      skip_clean = true;
      log_debug("%s: Skipping writing the page (%p fd %d) of all zeroes to externRAM.", __func__,
                pageaddr, ufd);
    }
    else
    {
#endif
      skip_clean = true;
      // write page to externram
#ifdef THREADED_WRITE_TO_EXTERNRAM
      log_lock("%s: locking list_lock", __func__);
      pthread_mutex_lock(&list_lock);
      log_lock("%s: locked list_lock", __func__);

      bool waiting = isWriterWaiting;
      add_write_info( ufd, (uint64_t)(uintptr_t)pageaddr, evict_tmp_page );
      log_debug("%s: the page %p was put on the write list", __func__, pageaddr);

      int list_size = get_write_list_size();

      log_lock("%s: unlocking list_lock", __func__);
      pthread_mutex_unlock(&list_lock);
      log_lock("%s: unlocked list_lock", __func__);

      if( waiting && list_size>=WRITE_BATCH_SIZE ) {
        sem_post(&writer_sem);
        log_lock("%s: sem_posted writer_sem", __func__);
      }
#else
      // not THREADED_WRITE_TO_EXTERNRAM
      struct externRAMClient *client = get_client_by_fd(ufd);
      if (client) {
        start_timing_bucket(start, WRITE_PAGE);
        write_ret = writePage(client, (uint64_t)(uintptr_t)pageaddr, evict_tmp_page_ptr);
        stop_timing(start, end, WRITE_PAGE);
        if (write_ret != NULL) {
          // externram wants us to free the buffer in write_ret, but
          // leave evict_tmp_page alone
          skip_clean = false;
          evict_tmp_page = write_ret;
        }
      }
      else
        log_err("%s: failed writing page %p for invalid fd %d", __func__, pageaddr, ufd);
#endif // THREADED_WRITE_TO_EXTERNRAM

#ifdef PAGECACHE
      start_timing_bucket(start, UPDATE_PAGE_CACHE);
      updatePageCacheAfterWrite(pageCache, ufd, (uint64_t)(uintptr_t)pageaddr);
      stop_timing(start, end, UPDATE_PAGE_CACHE);
#endif
#ifdef PAGECACHE_ZEROPAGE_OPTIMIZATION
    }
#endif
  }
  else if (ret == 2) {
    // tried evicting zeropage
    log_debug("%s: Skipping writing the zeropage at (%p fd %d) to externRAM.", __func__,
              pageaddr, ufd);
    ret = 2;

    // the page will be put back on LRU list, so don't update page cache
  }

  // cleanup
  if (!skip_clean) {
#ifdef THREADED_REINIT
    free_ret = return_free_page(buf_evictpage, evict_tmp_page);
#else
    free_ret = munmap(evict_tmp_page,PAGE_SIZE);
#endif
    if (free_ret < 0) {
      log_err("%s: munmap to %p", __func__, evict_tmp_page);
    }
    else {
      log_debug("%s: munmap to %p", __func__, evict_tmp_page);
    }
  }

  log_trace_out("%s", __func__);
  return ret;
}

int read_from_externram(int ufd, void * pageaddr) {
  log_trace_in("%s", __func__);
  log_debug("%s: reading page %p", __func__, pageaddr);

  // initialization & var init
  declare_timers();
  int length = -1;
  int ret = -1, ret2 = -1;
  int ret_munmap = -1;
#ifdef MONITORSTATS
  StatsIncrPageFault_notlocked();
#endif
  int numToEvict;
  void ** read_tmp_page_ptr = NULL;
  bool skip_read = false;
  void *temp_ptr = NULL;

#ifdef THREADED_WRITE_TO_EXTERNRAM
  while (true)
  {
    bool toWait = false;
    void *page_from_write_list = NULL;

    log_lock("%s: locking list_lock", __func__);
    pthread_mutex_lock(&list_lock);
    log_lock("%s: locked list_lock", __func__);

    bool waiting = isWriterWaiting;
    write_info * w = find_write_info(ufd,(uint64_t)(uintptr_t)pageaddr);
    if(w != NULL)
    {
      temp_ptr = extract_page_from_write_list(w);
      if (temp_ptr != NULL) {
        log_debug("%s: the page %p could be pulled off the write list", __func__, pageaddr);
#ifdef PAGECACHE
        updatePageCacheAfterSkippedRead( (uint64_t)(uintptr_t) pageaddr, ufd );
#endif
        read_tmp_page_ptr = &temp_ptr;
      }
      else {
        // failed to extract page. must be in-flight
        toWait = true;
        isUfhandlerWaiting = true;
      }
    }
    else
      isUfhandlerWaiting = false;

    log_lock("%s: unlocking list_lock", __func__);
    pthread_mutex_unlock(&list_lock);
    log_lock("%s: unlocked list_lock", __func__);

    if(toWait)
    {
      log_debug("%s: the page %p is on the write list, so should wait for it to be completed", __func__, pageaddr);
      if(waiting) {
        sem_post(&writer_sem);
        log_lock("%s: sem_posted writer_sem", __func__);
      }
      log_lock("%s: sem_waiting on ufhandler_sem", __func__);
      sem_wait(&ufhandler_sem);
      log_lock("%s: sem_waited on ufhandler_sem", __func__);
    }
    else
    {
      break;
    }
  }
#endif
#ifdef THREADED_PREFETCH
  while(true)
  {
    bool toWait = false;

    log_lock("%s: locking list_lock", __func__);
    pthread_mutex_lock(&list_lock);
    log_lock("%s: locked list_lock", __func__);

    bool waiting = isPrefetcherWaiting;
    if(exist_prefetch_info(ufd,(uint64_t)(uintptr_t)pageaddr))
    {
      log_debug("%s: found prefetch info for page %p and ufd %d", __func__, pageaddr, ufd);
      toWait = true;
      isUfhandlerWaiting = true;
    }
    else
      isUfhandlerWaiting = false;

    log_lock("%s: unlocking list_lock", __func__);
    pthread_mutex_unlock(&list_lock);
    log_lock("%s: unlocked list_lock", __func__);

    if(toWait)
    {
      log_debug("%s: the page %p is on the prefetch list, so should wait for it to be completed", __func__, pageaddr);
      if(waiting) {
        sem_post(&prefetcher_sem);
        log_lock("%s: sem_posted prefetcher_sem", __func__);
      }
      log_lock("%s: sem_waiting on ufhandler_sem", __func__);
      sem_wait(&ufhandler_sem);
      log_lock("%s: sem_waited on ufhandler_sem", __func__);
    }
    else
    {
      break;
    }
  }
#endif

#ifdef THREADED_PREFETCH
#ifdef ASYNREAD
  // we don't need to wait for prefetch thread, so take the lock
  log_lock("%s: locking read_lock", __func__);
  pthread_mutex_lock(&read_lock);
  log_lock("%s: locked read_lock", __func__);
#endif
#endif

  /*
     Set read_tmp_page_ptr (will be given to place_data_page()) to:
       1. the global read_tmp_page buffer
       2. the page buffer stolen from the write list
     This may be updated by functions that are passed read_tmp_page_ptr
   */
  if (read_tmp_page_ptr == NULL) {
    read_tmp_page_ptr = &read_tmp_page;
  }
  else {
    skip_read = true;
    length = PAGE_SIZE;
#ifdef ASYNREAD
    ret2 = evict_if_needed(ufd, pageaddr, ASYN_PAGE);
#endif
    goto place_page_out;
  }

#ifdef PAGECACHE
  log_lock("%s: locking pagecache_lock", __func__);
  pthread_mutex_lock(&pagecache_lock);
  log_lock("%s: locked pagecache_lock", __func__);

  start_timing_bucket(start, READ_VIA_PAGE_CACHE);
#ifdef ASYNREAD
  readPageIfInPageCache_top(pageCache, ufd, (uint64_t)(uintptr_t)pageaddr,
                            (void **)read_tmp_page_ptr);
#else
  length = readPageIfInPageCache(pageCache, ufd, (uint64_t)(uintptr_t)pageaddr,
                                 (void **)read_tmp_page_ptr);
#endif
  stop_timing(start, end, READ_VIA_PAGE_CACHE);

  log_lock("%s: unlocking pagecache_lock", __func__);
  pthread_mutex_unlock(&pagecache_lock);
  log_lock("%s: unlocked pagecache_lock", __func__);

#else
  // without page cache we don't know whether page has been seen before or not.
  struct externRAMClient *client = get_client_by_fd(ufd);
  if (client) {
    start_timing_bucket(start, READ_PAGE);
#ifdef ASYNREAD
    readPage_top(client, (uint64_t)(uintptr_t)pageaddr, read_tmp_page_ptr);
#else
    length = readPage(client, (uint64_t)(uintptr_t)pageaddr, read_tmp_page_ptr);
    stop_timing(start, end, READ_PAGE);
#endif
  }
  else
    log_err("%s: failed to read page %p for invalid fd %d", __func__, pageaddr, ufd);
#endif

#ifdef ASYNREAD
  // this is the case for when the page is not taken from the write list
  ret2 = evict_if_needed(ufd, pageaddr, ASYN_PAGE);

#ifdef PAGECACHE
  log_lock("%s: locking pagecache_lock", __func__);
  pthread_mutex_lock(&pagecache_lock);
  log_lock("%s: locked pagecache_lock", __func__);
  length = readPageIfInPageCache_bottom(pageCache, ufd, (uint64_t)(uintptr_t)pageaddr,
                                        (void **)read_tmp_page_ptr);
  stop_timing(start, end, READ_VIA_PAGE_CACHE);

  log_lock("%s: unlocking pagecache_lock", __func__);
  pthread_mutex_unlock(&pagecache_lock);
  log_lock("%s: unlocked pagecache_lock", __func__);

#else
  // already got client
  if (client) {
    length = readPage_bottom(client, (uint64_t)(uintptr_t)pageaddr, read_tmp_page_ptr);
    stop_timing(start, end, READ_PAGE);
  }
  else
    log_err("%s: failed to read page %p for invalid fd %d", __func__, pageaddr, ufd);
#endif
#endif

place_page_out:
  if (length == PAGE_SIZE) {
    ret = place_data_page(ufd, (void*)(uintptr_t)pageaddr, *read_tmp_page_ptr);
    if (ret < 0) {
      log_err("%s: place_data_page", __func__);
    }
    // ret = ufd if page eviction skipped
  } else if (length == 0){
    // place zero page
    ret = place_zero_page(ufd, (void *)(uintptr_t)pageaddr);
    if (ret < 0) {
      log_err("%s: place_zero_page", __func__);
    }
    // ret = ufd if page eviction skipped
  } else {
    log_err("%s: we don't know how to handle a read of length %s", __func__, length);
  }

  if (skip_read) {
    int ret_unmap = munmap(*read_tmp_page_ptr, PAGE_SIZE);
    if (ret_unmap < 0) {
      log_err("%s: munmap to %p", __func__, *read_tmp_page_ptr);
    }
    else {
      log_debug("%s: munmap to %p", __func__, *read_tmp_page_ptr);
    }
  }

#ifdef ASYNREAD
  // ret2 = ufd if page eviction skipped
  ret = ret2;

  // we have returned page back to the faulting applications, so now we can
  // resume asynchronous prefetch
  log_lock("%s: unlocking read_lock", __func__);
  pthread_mutex_unlock(&read_lock);
  log_lock("%s: unlocked read_lock", __func__);
#endif

  // now is also a good time to try and evict pages to get LRUbuffer to the proper size
  log_lock("%s: locking lru_lock", __func__);
  pthread_mutex_lock(&lru_lock);
  log_lock("%s: locked lru_lock", __func__);

  numToEvict = getLRUBufferSize(lru) - getLRUBufferMaxSize(lru);
  if (numToEvict > 0) {
    log_debug("%s: the LRUBuffer has %d more pages that it should. Calling evict_to_externram_multi.",
            __func__, numToEvict);
    ret2 = evict_to_externram_multi(numToEvict);

    log_lock("%s: unlocking lru_lock", __func__);
    pthread_mutex_unlock(&lru_lock);
    log_lock("%s: unlocked lru_lock", __func__);

    // print log message outside of lock scope
    if(ret2 != numToEvict) {
      log_debug(
               "%s: failed to evict some_pages. attempted=%d, evicted=%d",
              __func__, numToEvict, ret2);
    }
  }
  else {
    log_lock("%s: unlocking lru_lock", __func__);
    pthread_mutex_unlock(&lru_lock);
    log_lock("%s: unlocked lru_lock", __func__);
  }

#ifdef MONITORSTATS
  StatsSetLastFaultTime();
#endif

  log_trace_out("%s", __func__);
  return ret;
}

/*
 * Delete_from_externram store
 * This function will delete the page from externram
 *
 */
static inline int delete_from_externram(int ufd, externRAMClient *client, void * pageaddr) {
  log_trace_in("%s", __func__);

  int ret = 0;
  // delete page from externram
  if (client) {
    int ret = removePage(client, (uint64_t)(uintptr_t)pageaddr);
    if (ret != 1) {
      log_warn("%s: failed deleting page %p", __func__, pageaddr);
    }
  }
  else {
    log_err("%s: invalid externram client handle", __func__);
    ret = -1;
  }

  log_trace_out("%s", __func__);
  return ret;
}


int flush_buffers(int ufd, externRAMClient *client, int flush_or_delete) {
  log_trace_in("%s", __func__);

  uint64_t * page_list = NULL;
  int num_pages;
  int i, ret;

  // If we are flushing pages, only lru buffer has dirty pages to flush.
  num_pages = 0;

  log_lock("%s: locking lru_lock", __func__);
  pthread_mutex_lock(&lru_lock);
  log_lock("%s: locked lru_lock", __func__);

  page_list = removeUFDFromLRU(lru, ufd, &num_pages);

  log_lock("%s: unlocking lru_lock", __func__);
  pthread_mutex_unlock(&lru_lock);
  log_lock("%s: unlocked lru_lock", __func__);

  if (num_pages < 0) {
    log_err("%s: failure trying to remove entries from LRU", __func__);
  }

  if (flush_or_delete == FLUSH_TO_EXTERNRAM) {
    // in this case ufd better not have been removed from map
    for (i = 0; i < num_pages; i++) {
      ret = evict_to_externram(ufd, (void *)(uintptr_t)page_list[i]);
      if (ret == 0) {
        log_debug("%s: flush of page %p succeeded", __func__, (void*)(uintptr_t)page_list[i]);
      }
      else if (ret == 2) {
        log_debug("%s: flush of page %p delayed", __func__, (void*)(uintptr_t)page_list[i]);
      }
      else
        log_err("%s: flush of page %p failed", __func__, (void*)(uintptr_t)page_list[i]);
    }
  }

  if (num_pages > 0) {
    log_debug("%s: removed %d pages from LRU", __func__, num_pages);
    free(page_list);
  }


#ifdef PAGECACHE
  // Clean up pages in PageCache
  num_pages = 0;

  log_lock("%s: locking pagecache_lock", __func__);
  pthread_mutex_lock(&pagecache_lock);
  log_lock("%s: locked pagecache_lock", __func__);

  removeUFDFromPageCache(pageCache, ufd, &num_pages);
  if ( num_pages < 0) {
    log_err("%s: failed trying to remove entries for UFD %d from PageCache", __func__, ufd);
  }
  if (num_pages > 0) {
    log_debug("%s: removed %d pages from page cache", __func__, num_pages);
  }

  // Now clean up all pages that we've seen before (in pagehash)
  num_pages = 0;

  page_list = removeUFDFromPageHash(pageCache, ufd, &num_pages);
  if ( num_pages < 0) {
    log_err("%s: failed trying to remove entries for UFD %d from PageHash", __func__, ufd);
  }

  log_lock("%s: unlocking pagecache_lock", __func__);
  pthread_mutex_unlock(&pagecache_lock);
  log_lock("%s: unlocked pagecache_lock", __func__);

  // pages in page_list are from page hash. If process is dead, we
  // can use this list to delete them and free up space
  if (flush_or_delete == DELETE_FROM_EXTERNRAM) {
    for (i = 0; i < num_pages; i++) {
      ret = delete_from_externram(ufd, client, (void *)(uintptr_t)page_list[i]);
      if (ret == 0) {
        log_debug("%s: deletion of page %p succeeded", __func__, (void*)(uintptr_t)page_list[i]);
      }
      else if (ret == -1) {
        log_warn("%s: skipping deletion of page hash pages from externram for invalid ufd %d", __func__, ufd);
        break;
      }
      else
        log_err("%s: deletion of page %p failed", __func__, (void*)(uintptr_t)page_list[i]);
    }
  }

  if (num_pages > 0) {
    log_debug("%s: removed %d tracked pages", __func__, num_pages);
    free(page_list);
  }
#endif

  log_trace_out("%s", __func__);
  return num_pages;
}

void clean_up_lock() {
  log_trace_in("%s", __func__);

  pthread_mutex_destroy(&zh_lock);
  pthread_mutex_destroy(&lru_lock);
  pthread_mutex_destroy(&fdUpidMap_lock);
#ifdef PAGECACHE
  pthread_mutex_destroy(&pagecache_lock);
#endif
#ifdef THREADED_WRITE_TO_EXTERNRAM
  pthread_mutex_destroy(&flush_write_needed_lock);
  sem_destroy(&writer_sem);
  sem_destroy(&flushed_write_sem);
#endif
#ifdef THREADED_PREFETCH
  sem_destroy(&prefetcher_sem);
#ifdef ASYNREAD
  pthread_mutex_destroy(&read_lock);
#endif
#endif
#if defined(THREADED_WRITE_TO_EXTERNRAM) || defined(THREADED_PREFETCH)
  pthread_mutex_destroy(&list_lock);
  sem_destroy(&ufhandler_sem);
#endif
#ifdef THREADED_REINIT
  cleanup_page_buffer(buf_readpage);
  cleanup_page_buffer(buf_evictpage);
  return_free_page(buf_readpage, read_tmp_page);
#else
  munmap(read_tmp_page, PAGE_SIZE);
#endif
  log_trace_out("%s", __func__);
}

int create_server(char * socket_path) {
  log_trace_in("%s", __func__);

  struct sockaddr_un addr;
  int fd;

  if ((fd = socket(AF_LOCAL, SOCK_STREAM, 0)) < 0) {
    log_err("%s: failed to create server socket", __func__);
    return fd;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_LOCAL;

  unlink(socket_path);
  strcpy(addr.sun_path, socket_path);

  if (bind(fd, (struct sockaddr *) &(addr),
           sizeof(addr)) < 0) {
    log_err("%s: failed to bind server socket: %s", __func__, socket_path);
    return -1;
  }

  if (listen(fd, MAX_PENDING) < 0) {
    log_err("%s: failed to listen on server socket", __func__);
    return -1;
  }

  setnonblocking(fd);

  log_trace_out("%s", __func__);
  return fd;
}

int recv_fd(int socket) {
  log_trace_in("%s", __func__);

  int sent_fd=-1;
  pid_t sent_pid=-1;
  struct msghdr socket_message;
  struct iovec io_vector[1];
  struct cmsghdr *control_message = NULL;
  char message_buffer[4];
  char ancillary_element_buffer[CMSG_SPACE(sizeof(int))];
  int available_ancillary_element_buffer_space;
  int res;
  uint8_t upid[8];

  /* start clean */
  memset(&socket_message, 0, sizeof(struct msghdr));
  memset(&upid, 0, sizeof(uint8_t) * 8);
  available_ancillary_element_buffer_space = sizeof(ancillary_element_buffer);
  memset(ancillary_element_buffer, 0, available_ancillary_element_buffer_space);

  /* setup a place to fill in message contents */
  io_vector[0].iov_base = message_buffer;
  io_vector[0].iov_len = sizeof(message_buffer);
  socket_message.msg_iov = io_vector;
  socket_message.msg_iovlen = 1;

  /* provide space for the ancillary data */
  socket_message.msg_control = ancillary_element_buffer;
  socket_message.msg_controllen = available_ancillary_element_buffer_space;

  if ((res = recvmsg(socket, &socket_message, 0)) <= 0)
    return res;
  if ((socket_message.msg_flags & MSG_CTRUNC) == MSG_CTRUNC) {
    /* we did not provide enough space for the ancillary element array */
    return -1;
  }

  sent_pid = *((pid_t*) message_buffer);
  log_info("%s: received pid %d", __func__, sent_pid);

  /* iterate ancillary elements */
  for (control_message = CMSG_FIRSTHDR(&socket_message);
       control_message != NULL;
       control_message = CMSG_NXTHDR(&socket_message, control_message)) {
    if (control_message->cmsg_level == SOL_SOCKET) {
      if (control_message->cmsg_type == SCM_RIGHTS) {
        sent_fd = *((int *) CMSG_DATA(control_message));
        log_debug("%s: received fd %d", __func__, sent_fd);
      }
    }
  }

  /* create a upid */
  srand (time(NULL));
  uint16_t upid_counter = 0;
  uint16_t orig_value = 0;

  while(true) {
    int ret=0;
    uint64_t upid64=0;

    *((uint16_t*) &upid[0]) = get_node_id(); // node id hashed over 16 bits
    *((uint32_t*) &upid[2]) = (uint32_t) sent_pid; // pid value on the system

    upid_counter++;
    if (upid_counter == 1) {
      upid_counter = rand() % (2<<16); // random value up to 65k
      orig_value = upid_counter;
    }
    else if (upid_counter == orig_value) {
      // this could take a long time, but the idea is to reduce likelihood of failure
      log_warn("%s: tried all possibile values for upid with pid=%u and node_id=%hx, giving up on fd %d",
              __func__, *((uint32_t*) &upid[2]), *((uint16_t*) &upid[0]), sent_fd);
      sent_fd = -1;
      goto out;
    }
    else {
      if (upid_counter == 0) {
        // avoid ending up on 1 and resetting orig_value
        upid_counter++;
      }
    }
    *((uint16_t*) &upid[6]) = upid_counter;
    log_debug("%s: node_id=%hx pid=%u unique_counter=%hu",
              __func__, *((uint16_t*) &upid[0]), *((uint32_t*) &upid[2]), *((uint16_t*) &upid[6]));
    memcpy(&upid64, upid, sizeof(uint8_t) * 8);
    log_debug("%s: upid64=0x%llx", __func__, upid64);

    log_lock("%s: locking zh_lock", __func__);
    pthread_mutex_lock(&zh_lock);
    log_lock("%s: locked zh_lock", __func__);
    ret=add_upid(zookeeperConn, upid64);
    log_lock("%s: unlocking zh_lock", __func__);
    pthread_mutex_unlock(&zh_lock);
    log_lock("%s: unlocked zh_lock", __func__);

    if(ret==ZOOKEEPER_UPID_EXIST) {
      log_debug("%s: failed to add fd %d: upid 0x%llx already in zookeeper (%s). Trying with a different upid",
                __func__, sent_fd, upid64, zookeeperConn);
      continue;
    }
    else if(ret==ZOOKEEPER_UPID_OK)
    {
      add_upid_in_map(sent_fd, upid64);
      register_with_externram(config, sent_fd);
      break;
    } else if(ret==ZOOKEEPER_UPID_ERR)
    {
      log_err("%s: failed to add fd %d to zookeeper (%s)", __func__, sent_fd, zookeeperConn);
      sent_fd = -1;
      break;
    }
  }

out:
  log_trace_out("%s", __func__);
  return sent_fd;
}


void flush_write_list() {

#ifdef THREADED_WRITE_TO_EXTERNRAM
  // flush write_list
  log_lock("%s: locking flush_write_needed_lock", __func__);
  pthread_mutex_lock(&flush_write_needed_lock);
  log_lock("%s: locked flush_write_needed_lock", __func__);

  flushWriteListNeeded = true;

  log_lock("%s: unlocking flush_write_needed_lock", __func__);
  pthread_mutex_unlock(&flush_write_needed_lock);
  log_lock("%s: unlocked flush_write_needed_lock", __func__);

  sem_post(&writer_sem);
  log_lock("%s: sem_posted writer_sem", __func__);

  log_lock("%s: sem_waiting flushed_write_sem", __func__);
  sem_wait(&flushed_write_sem);
  log_lock("%s: sem_waited flushed_write_sem", __func__);
#endif

}

int flush_ufd(int ufd, struct externRAMClient *client) {
  log_trace_in("%s", __func__);

  int ret = 0;

  if (client) {
    if (flush_buffers(ufd, client, DELETE_FROM_EXTERNRAM) < 0) {
      log_debug("%s: failed to flush entries for dead ufd %d", __func__, ufd);
      return -1;
    }
  }
  else {
    log_warn("%s: couldn't get externram client handle for fd %d", __func__, ufd);
    ret = -1;
  }

  log_trace_out("%s", __func__);
  return ret;
}

int purgeDeadUpids(int ** ufd_list_ptr) {
  log_trace_in("%s", __func__);

  struct map_struct *current, *temp;
  uint8_t upid[8];
  uint32_t pid;
  uint16_t node_id = get_node_id();
  int num_dead_ufds = 0;
  int ret = 0;
  int temp_fd;
  struct externRAMClient *client;

  flush_write_list();

  log_lock("%s: locking fdUpidMap_lock", __func__);
  pthread_mutex_lock(&fdUpidMap_lock);
  log_lock("%s: locked fdUpidMap_lock", __func__);

  HASH_ITER(hh, fdUpidMap, current, temp) {
    memcpy(upid, &current->upid, 8);
    temp_fd = current->fd;

    if (*((uint16_t*) &upid[0]) == node_id) {
      // correct node
      pid = *((uint32_t*) &upid[2]);
      if (getpgid(pid) < 0) {
        // pid is dead

        // get externram client handle for use later
        client = current->client;

        if (del_fd_from_map(temp_fd) < 0) {
          log_warn("%s: failed to remove dead ufd %d from upid map", __func__, temp_fd);
        }
        else {
          log_debug("%s: removed ufd %d from the upid map", __func__, temp_fd);
        }

        // want fdUpidMap_lock unlocked for flush_ufd
        log_lock("%s: unlocking fdUpidMap_lock", __func__);
        pthread_mutex_unlock(&fdUpidMap_lock);
        log_lock("%s: unlocked fdUpidMap_lock", __func__);

        if (client) {
          if (flush_ufd(temp_fd, client) < 0) {
            log_warn("%s: failed to flush buffers for ufd %d", __func__, temp_fd);
          }
        }
        else {
          log_warn("%s: couldn't get externram client handle for fd %d", __func__, temp_fd);
        }

        if (remove_upid(*upid) < 0) {
          log_warn("%s: failed to remove dead upid 0x%llx", __func__, *upid);
        }

        // add fd to list for monitor to remove from poll list
        (*ufd_list_ptr)[num_dead_ufds] = temp_fd;
        num_dead_ufds++;

        // increase size by one for the next ufd
        (*ufd_list_ptr) = realloc((*ufd_list_ptr), sizeof(int) * (num_dead_ufds + 1));
        if(!(*ufd_list_ptr)) {
          log_err("%s: failed to increase size of ufd_list to %s", __func__, num_dead_ufds + 1);
        }
        log_lock("%s: locking fdUpidMap_lock", __func__);
        pthread_mutex_lock(&fdUpidMap_lock);
        log_lock("%s: locked fdUpidMap_lock", __func__);
      }
    }
  }

  log_lock("%s: unlocking fdUpidMap_lock", __func__);
  pthread_mutex_unlock(&fdUpidMap_lock);
  log_lock("%s: unlocked fdUpidMap_lock", __func__);

  log_trace_out("%s", __func__);
  return num_dead_ufds;
}

int listPids(uint32_t ** pid_list_ptr) {
  log_trace_in("%s", __func__);

  struct map_struct *current, *temp;
  uint8_t upid[8];
  uint32_t pid;
  uint16_t node_id = get_node_id();
  int num_pids = 0;

  log_lock("%s: locking fdUpidMap_lock", __func__);
  pthread_mutex_lock(&fdUpidMap_lock);
  log_lock("%s: locked fdUpidMap_lock", __func__);

  HASH_ITER(hh, fdUpidMap, current, temp) {
    memcpy(upid, &current->upid, 8);
    if (*((uint16_t*) &upid[0]) == node_id) {
      // correct node
      pid = *((uint32_t*) &upid[2]);

      // add pid to list for monitor to remove
      (*pid_list_ptr)[num_pids] = pid;
      num_pids++;

      // increase size by one for the next pid
      (*pid_list_ptr) = realloc((*pid_list_ptr), sizeof(uint32_t) * (num_pids + 1));
      if(!(*pid_list_ptr)) {
        log_err("%s: failed to increase size of pid_list to %s", __func__, num_pids + 1);
      }
    }
  }

  log_lock("%s: unlocking fdUpidMap_lock", __func__);
  pthread_mutex_unlock(&fdUpidMap_lock);
  log_lock("%s: unlocked fdUpidMap_lock", __func__);

  log_trace_out("%s", __func__);
  return num_pids;
}

int remove_upid(uint64_t upid) {
  int ret = 0;

  // remove upid from zookeeper
  log_lock("%s: locking zh_lock", __func__);
  pthread_mutex_lock(&zh_lock);
  log_lock("%s: locked zh_lock", __func__);

  if (del_upid(zookeeperConn, upid) < 0) {
    log_warn("%s: failed to remove dead upid 0x%llx from zookeeper", __func__, upid);
    ret = -1;
  }

  log_lock("%s: unlocking zh_lock", __func__);
  pthread_mutex_unlock(&zh_lock);
  log_lock("%s: unlocked zh_lock", __func__);

  return ret;
}


int removePid(uint32_t pidToRemove) {
  log_trace_in("%s", __func__);

  struct map_struct *current, *temp;
  uint8_t upid[8];
  uint32_t pid;
  uint16_t node_id = get_node_id();
  int rc = 0;
  int temp_fd = 0;
  struct externRAMClient *client;

  log_lock("%s: locking fdUpidMap_lock", __func__);
  pthread_mutex_lock(&fdUpidMap_lock);
  log_lock("%s: locked fdUpidMap_lock", __func__);

  HASH_ITER(hh, fdUpidMap, current, temp) {
    memcpy(upid, &current->upid, 8);
    temp_fd = current->fd;

    if (*((uint16_t*) &upid[0]) == node_id) {
      // correct node

      pid = *((uint32_t*) &upid[2]);
      if (pid == pidToRemove) {

        // get externram client handle for use later
        client = current->client;

        if (del_fd_from_map(temp_fd) < 0) {
          log_warn("%s: failed to remove dead ufd %d from upid map", __func__, temp_fd);
          rc = -1;
        }
        else {
          log_debug("%s: removed ufd %d from the upid map", __func__, temp_fd);
        }

        // want fdUpidMap_lock unlocked for flush_ufd
        log_lock("%s: unlocking fdUpidMap_lock", __func__);
        pthread_mutex_unlock(&fdUpidMap_lock);
        log_lock("%s: unlocked fdUpidMap_lock", __func__);

        log_info("%s: removed dead pid %u from map", __func__, pid);

        if (client) {
          if (flush_ufd(temp_fd, client) < 0) {
            log_warn("%s: failed to flush buffers for ufd %d", __func__, temp_fd);
            rc = -1;
          }
        }
        else {
          log_warn("%s: couldn't get externram client handle for fd %d", __func__, temp_fd);
          rc = -1;
        }

        if (remove_upid(*upid) < 0) {
          log_warn("%s: failed to remove dead upid 0x%llx", __func__, *upid);
          rc = -1;
        }

        // there's only one pid to remove, so we're done
        break;

        log_lock("%s: locking fdUpidMap_lock", __func__);
        pthread_mutex_lock(&fdUpidMap_lock);
        log_lock("%s: locked fdUpidMap_lock", __func__);
      }
    }
  }

  log_lock("%s: unlocking fdUpidMap_lock", __func__);
  pthread_mutex_unlock(&fdUpidMap_lock);
  log_lock("%s: unlocked fdUpidMap_lock", __func__);

  if (temp_fd == 0) {
    log_warn("%s: no pid %u found in upid map", __func__, pidToRemove);
    rc = -1;
  }

  log_trace_out("%s", __func__);
  return rc;
}

int getExternRAMUsage(ServerUsage ** usage_ptr) {
  log_trace_in("%s", __func__);

  struct map_struct *current, *temp;
  uint8_t upid[8];
  uint32_t pid;
  uint16_t node_id = get_node_id();
  int num_servers = 0;
  int fd = -1;

  log_lock("%s: locking fdUpidMap_lock", __func__);
  pthread_mutex_lock(&fdUpidMap_lock);
  log_lock("%s: locked fdUpidMap_lock", __func__);

  HASH_ITER(hh, fdUpidMap, current, temp) {
    memcpy(upid, &current->upid, 8);
    if (*((uint16_t*) &upid[0]) == node_id) {
      // found one ufd
      fd = current->fd;
      break;
    }
  }

  log_lock("%s: unlocking fdUpidMap_lock", __func__);
  pthread_mutex_unlock(&fdUpidMap_lock);
  log_lock("%s: unlocked fdUpidMap_lock", __func__);

  if (fd > 0) {
    struct externRAMClient *client = get_client_by_fd(fd);
    if (client) {
      num_servers = getUsage(client, usage_ptr);
    }
    else
      log_warn("%s: could not get stats for externram client with ufd %d", __func__, fd);
  }

  log_trace_out("%s", __func__);
  return num_servers;
}
