/*
 * Copyright 2015-2016 Blake Caldwell, University of Colorado
 * Copyright 2015-2016 Youngbin Im, University of Colorado
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Youngbin Im <Youngbin.Im@colorado.edu>, Sep 2015
 */

/*
 * PageCacheImpl.cc
 *
 * This file implements a concrete class of PageCache
*/

#include "PageCacheImpl.hh"
#include <dbg.h>

#undef check // For Boost 1.58 libraries

#include <stdio.h>
#include <cstdint>  // for uint types
#include <sys/user.h> /* for PAGE_MASK */
#include <boost/shared_ptr.hpp>
#include <boost/intrusive/list.hpp>
#include <assert.h>
#ifdef MONITORSTATS
#include <monitorstats.h>
#endif
#ifdef TIMING
#include <timingstats.h>
#endif
#include <upid.h>
#include <threaded_io.h>
#include <sys/user.h> /* for PAGE_SIZE */
#include <buffer_allocator_array.h>

struct LRUBuffer *           PageCache::lruBuffer=NULL;

/*
 *** ::create() ****
 * This is the Pointer to Implementation Pattern
 ********************************
 */
PageCache * PageCache::create( struct LRUBuffer *lru )
{
  log_trace_in("%s", __func__);
  /*
   * Necesssary to use null_deleter or object will get implicitly deleted.
   */
  boost::shared_ptr<PageCache> impl(new PageCacheImpl(),null_deleter());
  lruBuffer = lru;

#ifdef PAGECACHE_ZEROPAGE_OPTIMIZATION
  log_info("%s: page cache zero page optimization enabled.", __func__);
#else
  log_info("%s: page cache zero page optimization disabled.", __func__);
#endif
  log_trace_out("%s", __func__);
  return (impl.get());
}


/*
 *** PageCacheImpl::PageCacheImpl() ***
 *  default parameterless constructor
 **********************************************
 */
PageCacheImpl::PageCacheImpl()
{
  log_debug("%s: PageCache: created instance of PageCache", __func__);
}


/*
 *** PageCacheImpl::~PageCacheImpl() ***
 *  default parameterless constructor
 **********************************************
 */
PageCacheImpl::~PageCacheImpl()
{
  log_debug("%s: PageCache: deleted instance of LRU Buffer", __func__);
}

void PageCacheImpl::cleanup()
{
  log_trace_in("%s", __func__);

  log_debug("%s: pageCache size before cleanup :%d", __func__, pageCache.getSize());

  for( int i=0; i<pageCache.getSize(); i++ )
  {
    popLRU();
  }

  log_debug("%s: pageCache size after cleanup :%d", __func__, pageCache.getSize());

  log_trace_out("%s", __func__);
}

void PageCacheImpl::referencePageCachedNode(uint64_t key, int fd) {
  log_trace_in("%s", __func__);
  /**** NOT USED ****/

  // move the referenced cache to the front
  pageCache.insert(*pageCache.find(key,fd));

  log_trace_out("%s", __func__);
}

void PageCacheImpl::insertPageCacheNode(uint64_t key,int fd,void * addr,int size) {
  log_trace_in("%s", __func__);

  assert( addr!=NULL && size==PAGE_SIZE );

  // Populate an instance of page_cache_node
  page_cache_node node;
  node.hashcode = key;
  node.address = addr;
  node.size = size;
  node.fd = fd;

  pageCache.insert(node);

  log_trace_out("%s", __func__);
}

uint64_t PageCacheImpl::popLRU() {
  log_trace_in("%s", __func__);

  uint64_t key;
  void * address;
  int size;
  int fd;

  // Populate an instance of page_cache_node with the last entry
  page_cache_node node = pageCache.back();
  key = node.hashcode;
  address = node.address;
  size = node.size;
  fd = node.fd;

  // Release allocated memory and pop the last entry
  free((void*) address);
  pageCache.pop_back();
  changeOwnership( key, fd, OWNERSHIP_EXTERNRAM, false);

  log_debug("%s: LRU pop from page cache, hashcode=%lx, fd=%d, buf=%lx, length=%d", __func__, key, fd, (long unsigned int)address, size);

  log_trace_out("%s", __func__);
  return key;
}

int PageCacheImpl::isLRUSizeExceeded() {
  log_trace_in("%s", __func__);

  if( pageCache.isSizeExceeded() )
    return 1;
  else
    return 0;

  log_trace_out("%s", __func__);
}

inline void PageCacheImpl::changeOwnership( uint64_t hashcode, int fd, int ownership, bool is_zeropage=false )
{
  log_trace_in("%s", __func__);

  char t[sizeof(uint64_t)+sizeof(int)];
  *((uint64_t*) &t[0]) = hashcode;
  *((int*) &t[sizeof(uint64_t)]) = fd;
  std::string k(t,sizeof(uint64_t)+sizeof(int));
  page_hash::iterator itr = pagehash.find(k);
  if( itr!=pagehash.end() )
  {
    itr->second->ownership = ownership;
    itr->second->is_zeropage = is_zeropage;
    log_debug("%s: The ownership of page %lx fd %d has changed to %d (is_zeropage : %d)", __func__, hashcode, fd, ownership, is_zeropage );
  }
  else
  {
    log_err("%s: Cannot find the entry for page %lx fd %d in pagehash", __func__, hashcode, fd );
  }

  log_trace_out("%s", __func__);
}

inline void PageCacheImpl::changeOwnershipWithItr( page_hash::iterator itr, int ownership, bool is_zeropage=false )
{
  log_trace_in("%s", __func__);

  if( itr!=pagehash.end() )
  {
    itr->second->ownership = ownership;
    itr->second->is_zeropage = is_zeropage;
  }
  else
  {
      log_err("%s: Cannot find the entry for page in pagehash", __func__);
  }

  log_trace_out("%s", __func__);
}

int PageCacheImpl::readPageIfInPageCache( uint64_t hashcode, int fd, void ** buf )
{
  log_trace_in("%s", __func__);
  int size = 0;
  readPageIfInPageCache_top( hashcode, fd, buf );
  size = readPageIfInPageCache_bottom( hashcode, fd, buf );
  log_trace_out("%s", __func__);
  return size;
}

void PageCacheImpl::readPageIfInPageCache_top( uint64_t hashcode, int fd, void ** buf )
{
  log_trace_in("%s", __func__);

  static uint64_t prevAddr = 0; // previously accessed address
  static int numConseqAcc = 0; // number of consequtive accesses

  if(prevAddr + PAGE_SIZE == hashcode)
    numConseqAcc++;
  else
    numConseqAcc=0;
  prevAddr = hashcode;

  char t[sizeof(uint64_t)+sizeof(int)];
  *((uint64_t*) &t[0]) = hashcode;
  *((int*) &t[sizeof(uint64_t)]) = fd;
  std::string k(t,sizeof(uint64_t)+sizeof(int));
  page_hash::iterator itr = pagehash.find(k);
  if( itr!=pagehash.end() && itr->second->ownership==OWNERSHIP_PAGE_CACHE )
  {

    // increment the page cache hit stat
#ifdef MONITORSTATS
    StatsIncrCacheHit_notlocked();
#endif

  }
  else if( itr!=pagehash.end() && itr->second->ownership==OWNERSHIP_EXTERNRAM )
  {
    // increment the page cache miss stat
#ifdef MONITORSTATS
    StatsIncrCacheMiss_notlocked();
#endif

    // If the page is in externRAM, return the page in externRAM
    if( !enable_prefetch )
    {
#ifdef PAGECACHE_ZEROPAGE_OPTIMIZATION
      if( itr->second->is_zeropage==true )
      {
      }
      else
      {
#endif
        struct externRAMClient *client = get_client_by_fd(fd);
        if (client) {
          // actually reading the page from externram, so we must allocate a page buffer

          start_timing_bucket(g_start, READ_PAGE);
#ifdef ASYNREAD
          log_debug("%s: preparing read for single page with key %lx", __func__, hashcode);
          readPage_top( client, hashcode, buf );
#endif
        }
        else
          log_err("%s: failed to read page %lx for invalid fd %d", __func__, hashcode, fd);
#ifdef PAGECACHE_ZEROPAGE_OPTIMIZATION
      }
#endif
    }
    else
    {
      // prefetch is enabled
#ifdef PAGECACHE_ZEROPAGE_OPTIMIZATION
      if( itr->second->is_zeropage==true )
      {
        goto read_page_if_in_page_cache_out_top;
      }
#endif

      // prepare prefetch structures
      float avgSequentialAccessNum = 10;
      int i=1;
      numPrefetch = 0;
      keys_for_mread[0] = hashcode;

#if defined(THREADED_WRITE_TO_EXTERNRAM) || defined(THREADED_PREFETCH)
      log_lock("%s: unlocking list_lock", __func__);
      pthread_mutex_lock(&list_lock);
      log_lock("%s: unlocked list_lock", __func__);
#endif
#ifdef THREADED_PREFETCH
      bool waiting = isPrefetcherWaiting;
#endif
      while( numPrefetch<numConseqAcc )
      {
        log_debug("%s: passed criteria for prefetch: will fetch %d keys after %d consecutive accesses", __func__, numPrefetch, numConseqAcc);

        uint64_t testaddr = hashcode + i * PAGE_SIZE;
        char t[sizeof(uint64_t)+sizeof(int)];
        *((uint64_t*) &t[0]) = testaddr;
        *((int*) &t[sizeof(uint64_t)]) = fd;
        std::string k(t,sizeof(uint64_t)+sizeof(int));
        page_hash::iterator itr2 = pagehash.find(k);
#ifdef THREADED_WRITE_TO_EXTERNRAM
        bool existInWriteList = exist_write_info(fd,testaddr);
#endif
#ifdef THREADED_PREFETCH
        bool existInPrefetchList = exist_prefetch_info(fd,testaddr);
#endif
        if( itr2!=pagehash.end() && itr2->second->ownership==OWNERSHIP_EXTERNRAM
#ifdef PAGECACHE_ZEROPAGE_OPTIMIZATION
          // Let's not bring zero pages to the cache since they will
          // be quickly retrieved if needed
          && itr2->second->is_zeropage==false
#endif
#ifdef THREADED_WRITE_TO_EXTERNRAM
          && !existInWriteList
#endif
#ifdef THREADED_PREFETCH
          && !existInPrefetchList
#endif
          )
        {
#ifdef THREADED_PREFETCH
          add_prefetch_info( fd, testaddr );
#else
          keys_for_mread[numPrefetch+1] = testaddr;

#endif // THREADED_PREFETCH
          numPrefetch++;
          log_debug("%s: Prefetching a page %lx fd %d.", __func__, testaddr, fd);
        }

        if( numPrefetch>=prefetch_size ) {
          log_debug("%s: breaking prefetch up because numPrefetch has reached prefetch_size", __func__, prefetch_size);
          break;
        }
        if ( i>100 ) {
          // TODO: can this be removed?
          log_debug("%s: breaking prefetch up because %d pages have been added already", __func__, i);
          break;
        }
        i++;
      }
      buf = bufs_for_mread;
      // prefetch structures prepared

#if defined(THREADED_WRITE_TO_EXTERNRAM) || defined(THREADED_PREFETCH)
#ifdef THREADED_PREFETCH
      int list_size = get_prefetch_list_size();
#endif
      log_lock("%s: unlocking list_lock", __func__);
      pthread_mutex_unlock(&list_lock);
      log_lock("%s: unlocked list_lock", __func__);
#endif // defined(THREADED_WRITE_TO_EXTERNRAM) || defined(THREADED_PREFETCH)

      // start the prefetch in externram
      struct externRAMClient *client = get_client_by_fd(fd);
      if (client) {
#ifdef THREADED_PREFETCH
        if( waiting )
          sem_post(&prefetcher_sem);

        start_timing_bucket(g_start, READ_PAGE);
#ifdef ASYNREAD
        readPage_top( client, hashcode, buf );
#endif
#else
        start_timing_bucket(g_start, READ_PAGES);
#ifdef ASYNREAD
        readPages_top( client, keys_for_mread, numPrefetch+1, (void**) bufs_for_mread, lengths_for_mread );
#endif
#endif // THREADED_PREFETCH
      }
    }
  }
  else if( itr==pagehash.end() )
  {

    // increment the page cache miss stat
#ifdef MONITORSTATS
    StatsIncrCacheMiss_notlocked();
#endif
  }

read_page_if_in_page_cache_out_top:

  log_trace_out("%s", __func__);
}

int PageCacheImpl::readPageIfInPageCache_bottom( uint64_t hashcode, int fd, void ** buf )
{
  log_trace_in("%s", __func__);

  int size = 0;

  char t[sizeof(uint64_t)+sizeof(int)];
  *((uint64_t*) &t[0]) = hashcode;
  *((int*) &t[sizeof(uint64_t)]) = fd;
  std::string k(t,sizeof(uint64_t)+sizeof(int));
  page_hash::iterator itr = pagehash.find(k);

  // TODO this second pagehash lookup could be avoided
  if( itr!=pagehash.end() && itr->second->ownership==OWNERSHIP_PAGE_CACHE )
  {
    // If the page is in the cache, return the page in cache
    List::iterator itr2 = pageCache.find( hashcode, fd );
    if( itr2!=pageCache.findEnd() )
    {
      *buf = itr2->address;
      size = itr2->size;
    }
    pageCache.erase( hashcode, fd );
    changeOwnershipWithItr( itr, OWNERSHIP_APPLICATION );
    log_debug("%s: Cache hit for page %lx fd %d.", __func__, hashcode, fd);
  }
  else if( itr!=pagehash.end() && itr->second->ownership==OWNERSHIP_EXTERNRAM )
  {
    // If the page is in externRAM, return the page in externRAM
    if( !enable_prefetch )
    {
#ifdef PAGECACHE_ZEROPAGE_OPTIMIZATION
      if( itr->second->is_zeropage==true )
      {
        log_debug("%s: Skipping reading the page %lx fd %d from externRAM.", __func__, hashcode, fd);
      }
      else
      {
#endif
        struct externRAMClient *client = get_client_by_fd(fd);
        if (client) {
#ifdef ASYNREAD
          size = readPage_bottom( client, hashcode, buf );
#else
          size = readPage( client, hashcode, buf );
#endif // ASYNREAD
          stop_timing(g_start, g_end, READ_PAGE);
	}
        else
          log_err("%s: failed to read page %lx for invalid fd %d", __func__, hashcode, fd);
#ifdef PAGECACHE_ZEROPAGE_OPTIMIZATION
      }
#endif
      changeOwnershipWithItr( itr, OWNERSHIP_APPLICATION );
    }
    else
    {
      // prefetch is enabled
#ifdef PAGECACHE_ZEROPAGE_OPTIMIZATION
      if( itr->second->is_zeropage==true )
      {
        log_debug("%s: Skipping reading the all-zero page %lx fd %d from externRAM.", __func__, hashcode, fd);
        changeOwnershipWithItr( itr, OWNERSHIP_APPLICATION );
        goto read_page_if_in_page_cache_out_bottom;
      }
#endif
      struct externRAMClient *client = get_client_by_fd(fd);
      if (client) {
#ifdef THREADED_PREFETCH
#ifdef ASYNREAD
        size = readPage_bottom( client, hashcode, buf );
#else
        size = readPage( client, hashcode, buf );
#endif // ASYNREAD
        stop_timing(g_start, g_end, READ_PAGE);

        changeOwnershipWithItr( itr, OWNERSHIP_APPLICATION );
#else
#ifdef ASYNREAD
        readPages_bottom( client, keys_for_mread, numPrefetch+1, (void**) bufs_for_mread, lengths_for_mread );
#else
        readPages( client, keys_for_mread, numPrefetch+1, (void**) bufs_for_mread, lengths_for_mread );
#endif // ASYNREAD
        stop_timing(g_start, g_end, READ_PAGES);

        if( bufs_for_mread[0]!=NULL )
        {
          size = lengths_for_mread[0];
          *buf = bufs_for_mread[0];
          changeOwnershipWithItr( itr, OWNERSHIP_APPLICATION );
        }
        else {
          log_err("%s: failed to get the first page %p in multiread which is page faulted on by %d",
                  __func__, keys_for_mread[0], fd);
        }
#ifdef DEBUG
        for(int i=0; i<numPrefetch+1; i++)
        {
          log_debug("%s: multiRead [%d] bufs_for_mread %lx lenths_for_mread %d", __func__, i,(uint64_t)bufs_for_mread[i], lengths_for_mread[i]);
        }
#endif
        if( numPrefetch>0 )
        {
          storePagesInPageCache(&keys_for_mread[1], fd, numPrefetch, (char**) &bufs_for_mread[1], &lengths_for_mread[1]);
        }
#endif // THREADED_PREFETCH
      }
      else
        log_warn("%s: failed to read page %lx with invalid fd %d", __func__, hashcode, fd);
    }
    log_debug("%s: Cache miss! Read the page %lx fd %d from externRAM.", __func__, hashcode, fd);
  }
  else if( itr==pagehash.end() )
  {
    // zeropage if the page is not either in cache or externRAM
    start_timing_bucket(g_start, INSERT_PAGE_HASH_NODE);
    addPageHashNode( hashcode, fd, OWNERSHIP_APPLICATION);
    stop_timing(g_start, g_end, INSERT_PAGE_HASH_NODE);

    size = 0;
    log_debug("%s: Cache miss! New page %lx fd %d.", __func__, hashcode, fd);
  }
  else
  {
    log_err("%s: Pagehash error! should not happen!", __func__);
  }

read_page_if_in_page_cache_out_bottom:

  log_trace_out("%s", __func__);
  return size;
}

void PageCacheImpl::updatePageCacheAfterWrite( uint64_t hashcode, int fd, bool zeroPage)
{
  log_trace_in("%s", __func__);

  char t[sizeof(uint64_t)+sizeof(int)];
  *((uint64_t*) &t[0]) = hashcode;
  *((int*) &t[sizeof(uint64_t)]) = fd;
  std::string k(t,sizeof(uint64_t)+sizeof(int));
  page_hash::iterator itr = pagehash.find(k);

  if( itr!=pagehash.end() && ( itr->second->ownership==OWNERSHIP_PAGE_CACHE || itr->second->ownership==OWNERSHIP_EXTERNRAM ))
  {
    log_err("%s: Trying to write a page whose ownership location is pagecache or enternram!", __func__);
  }
  else if( itr!=pagehash.end() && itr->second->ownership==OWNERSHIP_APPLICATION )
  {
      changeOwnershipWithItr( itr, OWNERSHIP_EXTERNRAM, zeroPage );
  }
  else
  {
    log_err("%s: Page was not found!", __func__);
  }

  log_trace_out("%s", __func__);
}

void PageCacheImpl::updatePageCacheAfterSkippedRead( uint64_t hashcode, int fd)
{
  log_trace_in("%s", __func__);

  char t[sizeof(uint64_t)+sizeof(int)];
  *((uint64_t*) &t[0]) = hashcode;
  *((int*) &t[sizeof(uint64_t)]) = fd;
  std::string k(t,sizeof(uint64_t)+sizeof(int));
  page_hash::iterator itr = pagehash.find(k);

  if( itr!=pagehash.end() && ( itr->second->ownership==OWNERSHIP_PAGE_CACHE || itr->second->ownership==OWNERSHIP_APPLICATION))
  {
    log_err("%s: Trying to update a page that didn't have ownership externram!", __func__);
  }
  else if( itr!=pagehash.end() && itr->second->ownership==OWNERSHIP_EXTERNRAM )
  {
    changeOwnershipWithItr( itr, OWNERSHIP_APPLICATION, false );
  }
  else
  {
    log_err("%s: Page was not found!", __func__);
  }

  log_trace_out("%s", __func__);
}

void PageCacheImpl::addPageHashNode( uint64_t hashcode, int fd, int ownership )
{
  log_trace_in("%s", __func__);

  boost::shared_ptr<PageInfo> pi(new PageInfo(),null_deleter());
  pi->ref_count = 0;
  pi->ownership = ownership;

  char t[sizeof(uint64_t)+sizeof(int)];
  *((uint64_t*) &t[0]) = hashcode;
  *((int*) &t[sizeof(uint64_t)]) = fd;
  std::string k(t,sizeof(uint64_t)+sizeof(int));
  pagehash[k] = pi;

  log_debug("%s: Adding a new page hash node for page %lx fd %d with ownership %d", __func__, hashcode, fd, ownership);

  log_trace_out("%s", __func__);
}

void PageCacheImpl::storePageInPageCache(uint64_t hashcode, int fd, void * buf, int length)
{
  log_trace_in("%s", __func__);

  if (length != PAGE_SIZE) {
    log_err("%s: length is %d, not PAGE_SIZE", __func__, length);
  }
  assert( buf!=NULL );

  char t[sizeof(uint64_t)+sizeof(int)];
  *((uint64_t*) &t[0]) = hashcode;
  *((int*) &t[sizeof(uint64_t)]) = fd;
  std::string k(t,sizeof(uint64_t)+sizeof(int));
  page_hash::iterator itr = pagehash.find(k);

  if( itr!=pagehash.end() )
  {
    List::iterator itr2 = pageCache.find( hashcode, fd );

    if( itr2!=pageCache.findEnd() )
    {
      free( itr2->address );
      pageCache.modify( hashcode, buf, length, fd );
      log_err("%s: Updating the page cache(should not happen), hashcode=%lx, buf=%lx, length=%d, fd=%d", __func__, hashcode, (uint64_t) buf, length, fd);
    }
    else
    {
      insertPageCacheNode( hashcode, fd, buf, length );
      log_debug("%s: Adding a page cache, hashcode=%lx, fd=%d, buf=%lx, length=%d", __func__, hashcode, fd, (uint64_t) buf, length);
    }

    changeOwnershipWithItr( itr, OWNERSHIP_PAGE_CACHE );
  }
  else
  {
    addPageHashNode( hashcode, fd, OWNERSHIP_PAGE_CACHE );
    insertPageCacheNode( hashcode, fd, buf, length );
    log_debug("%s: Adding a page cache, hashcode=%lx, fd=%d, buf=%lx, length=%d", __func__, hashcode, fd, (uint64_t) buf, length);
  }

  if(isLRUSizeExceeded())
  {
    popLRU();
  }

  log_trace_out("%s", __func__);
}

void PageCacheImpl::storePagesInPageCache(uint64_t * hashcodes, int fd, int num_pages, char ** bufs, int * lengths)
{
  log_trace_in("%s", __func__);

  for( int i=0; i<num_pages; i++ )
  {
    storePageInPageCache(hashcodes[i], fd, (char*)*(bufs+i), lengths[i] );
  }

  log_trace_out("%s", __func__);
}

void PageCacheImpl::invalidatePageCache( uint64_t hashcode, int fd )
{
  log_trace_in("%s", __func__);

  List::iterator itr = pageCache.find( hashcode, fd );
  if( itr!=pageCache.findEnd() )
    free( itr->address );

  pageCache.erase( hashcode, fd );
  changeOwnership( hashcode, fd, OWNERSHIP_EXTERNRAM );

  log_debug("%s: removed page cache entry for %lx fd %d", __func__, hashcode, fd);

  log_trace_out("%s", __func__);
}

uint64_t * PageCacheImpl::removeUFDFromPageHash(int fd, int * numPages) {
  log_trace_in("%s", __func__);

  std::vector<uint64_t> keyVector;
  uint64_t * keyList;
  const char * t;
  uint64_t hashcode;
  int ufd;

  page_hash::iterator itr = pagehash.begin();
  int i=0;
  for ( ; itr!= pagehash.end(); itr++, i++ ) {
    t = itr->first.c_str();
    hashcode = *((uint64_t*) &t[0]);
    ufd = *((int*) &t[sizeof(uint64_t)]);
    if (ufd == fd) {
      // only put on keyVector if it is in externram
      if( itr->second->ownership==OWNERSHIP_EXTERNRAM ) {
        keyVector.push_back(hashcode & (uint64_t)(PAGE_MASK));
      }
      pagehash.erase(itr);
    }
  }
  *numPages = keyVector.size();

  // populate the memory region to be returned to libuserfault
  keyList = (uint64_t *)malloc(keyVector.size() * sizeof(uint64_t));
  for(std::vector<uint64_t>::size_type i = 0; i != keyVector.size(); i++) {
     memcpy(&keyList[i], &keyVector[i], sizeof(uint64_t));
  }

  log_trace_out("%s", __func__);
  return keyList;
}

void PageCacheImpl::removeUFDFromPageCache(int fd, int * numPages) {
  log_trace_in("%s", __func__);

  std::vector<uint64_t> keyVector;

  page_item_list::iterator itr = pageCache.begin();
  int i=0;
  for ( ; itr!= pageCache.end(); itr++, i++ ) {
    if (itr->fd == fd) {
      // this node will be removed
      keyVector.push_back(itr->hashcode & (uint64_t)(PAGE_MASK));
      log_debug("%s: freeing %p from ufd %d page cache entry for 0x%lx", __func__,
               itr->address, fd, itr->hashcode, fd);
      free( itr->address );
      pageCache.erase(itr->hashcode, fd);
    }
  }
  *numPages = keyVector.size();

  log_trace_out("%s", __func__);
}
