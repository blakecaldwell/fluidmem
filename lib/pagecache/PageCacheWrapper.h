/*
 * Copyright 2015 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, May 2015
 */

#ifndef PAGECACHEWRAPPER_H
#define PAGECACHEWRAPPER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// These represent where the most recent page versions are.
// If a page is in the application, page cache, and externram, this
// should be set to OWNERSHIP_APPLICATION since the application can
// modify the page.
#define OWNERSHIP_APPLICATION 1
#define OWNERSHIP_PAGE_CACHE  2
#define OWNERSHIP_EXTERNRAM   3

typedef struct PageCache PageCache;
PageCache* newPageCache(struct LRUBuffer *lru);

int readPageIfInPageCache( PageCache * pageCache, int ufd, uint64_t hashcode, void** buf );
#ifdef ASYNREAD
void readPageIfInPageCache_top( PageCache * pageCache, int ufd, uint64_t hashcode, void** buf );
int readPageIfInPageCache_bottom( PageCache * pageCache, int ufd, uint64_t hashcode, void** buf );
#endif
void updatePageCacheAfterWrite( PageCache * pageCache, int ufd, uint64_t hashcode);
void updatePageCacheAfterSkippedWrite( PageCache * pageCache, int ufd, uint64_t hashcode);
void invalidatePageCache( PageCache * pageCache, int ufd, uint64_t hashcode );
void addPageHashNode( uint64_t hashcode, int fd, int ownership );
void pageCacheCleanup();
void storePagesInPageCache( PageCache * pageCache, uint64_t * hashcodes, int fd, int num_pages, char ** bufs, int * lengths);
void removeUFDFromPageCache( PageCache * pageCache, int fd, int * numPages );
uint64_t * removeUFDFromPageHash( PageCache * pageCache, int fd, int * numPages );

#ifdef __cplusplus
}
#endif

#endif
