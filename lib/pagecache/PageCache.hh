/*
 * Copyright 2015 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, May 2015
 */

/*
 * PageCache.hh
 * This defines the abstract interface for interacting with the Page Cache
 */

#ifndef PAGECACHE_HH
#define PAGECACHE_HH
#include <stdint.h>
#include <iostream>
#include <../externram/externRAMClient.hh>
#include <../lrubuffer/LRUBuffer.hh>

// Abstract interface
class PageCache
{
public:
    static struct externRAMClient *     extRamClient;
    static struct LRUBuffer *           lruBuffer;

    virtual      ~PageCache(){};
    virtual void cleanup(){};

    // Pimpl pattern
    static PageCache *          create(struct LRUBuffer *lru);

    // API with clients
    virtual int                 readPageIfInPageCache( uint64_t hashcode, int fd, void** buf ){};
    virtual void                readPageIfInPageCache_top( uint64_t hashcode, int fd, void** buf ){};
    virtual int                 readPageIfInPageCache_bottom( uint64_t hashcode, int fd, void** buf ){};
    virtual void                updatePageCacheAfterWrite( uint64_t hashcode, int fd, bool zeroPage ){};
    virtual void                invalidatePageCache( uint64_t hashcode, int fd ){};
    virtual void                addPageHashNode( uint64_t hashcode, int fd, int ownership ){};
    virtual void                storePagesInPageCache( uint64_t * hashcodes, int fd, int num_pages, char ** bufs, int * lengths){};
    virtual void                removeUFDFromPageCache(int fd, int * numPages){};
    virtual uint64_t *          removeUFDFromPageHash(int fd, int * numPages){};

//protected:
    PageCache(){};
    PageCache(const PageCache &o);
    const PageCache & operator =(const PageCache &o);
};
#endif
