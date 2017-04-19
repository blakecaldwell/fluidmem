/*
 * Copyright 2015 Blake Caldwell, University of Colorado
 * Copyright 2015 Youngbin Im, University of Colorado
 * All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, May 2015
 */

/*
 * PageCacheWrapper is a C wrapper to the C++ class PageCache
 */

#include "PageCacheWrapper.h"
#include "PageCache.hh"
#include <stdio.h>

extern "C" {
  PageCache * pageCache;
  PageCache * newPageCache(struct LRUBuffer *lru) {
    pageCache = PageCache::create(lru);
    return pageCache;
  }

  int readPageIfInPageCache( PageCache * pageCache, int ufd, uint64_t hashcode, void** buf )
  {
    return pageCache->readPageIfInPageCache( hashcode, ufd, buf );
  }
#ifdef ASYNREAD
  void readPageIfInPageCache_top( PageCache * pageCache, int ufd, uint64_t hashcode, void** buf )
  {
    pageCache->readPageIfInPageCache_top( hashcode, ufd, buf );
  }
  int readPageIfInPageCache_bottom( PageCache * pageCache, int ufd, uint64_t hashcode, void** buf )
  {
    return pageCache->readPageIfInPageCache_bottom( hashcode, ufd, buf );
  }
#endif
  void updatePageCacheAfterWrite( PageCache * pageCache, int ufd, uint64_t hashcode )
  {
    pageCache->updatePageCacheAfterWrite( hashcode, ufd, false );
  }
  void updatePageCacheAfterSkippedWrite( PageCache * pageCache, int ufd, uint64_t hashcode )
  {
    pageCache->updatePageCacheAfterWrite( hashcode, ufd, true );
  }
  void invalidatePageCache( PageCache * pageCache, int ufd, uint64_t hashcode )
  {
    pageCache->invalidatePageCache( hashcode, ufd );
  }
  void addPageHashNode( uint64_t hashcode, int ufd, int ownership )
  {
    pageCache->addPageHashNode( hashcode, ufd, ownership );
  }
  void pageCacheCleanup()
  {
    pageCache->cleanup();
  }
  void storePagesInPageCache( PageCache * pageCache, uint64_t * hashcodes, int fd, int num_pages, char ** bufs, int * lengths)
  {
    pageCache->storePagesInPageCache( hashcodes, fd, num_pages, bufs, lengths);
  }
  uint64_t * removeUFDFromPageCache( PageCache * pageCache, int fd, int * numPages )
  {
    pageCache->removeUFDFromPageCache( fd, numPages);
  }
  uint64_t * removeUFDFromPageHash( PageCache * pageCache, int fd, int * numPages )
  {
    pageCache->removeUFDFromPageHash( fd, numPages);
  }
}
