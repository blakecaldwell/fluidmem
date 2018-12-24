/*
 * Copyright 2015 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, May 2015
 */

/*
 * LRUBuffer.hh
 * This defines the abstract interface for interacting with the LRU buffer
 */

#ifndef LRUBUFFER_HH
#define LRUBUFFER_HH
#include <stdint.h>
#include <iostream>
#include "c_cache_node.h"

// Abstract interface
class LRUBuffer
{
public:

    virtual         ~LRUBuffer(){};

    // Pimpl pattern
    static LRUBuffer *        create();

    // API with clients
    virtual struct c_cache_node insertCacheNode(uint64_t key, int ufd){};
    virtual void                referenceCachedNode(uint64_t key, int ufd){};
    virtual int                 popNLRU(int num_pop, c_cache_node ** node_list){};
    virtual struct c_cache_node getLRU(){};
    virtual int                 isLRUSizeExceeded(void){};
    virtual int                 getSize(){};
    virtual int                 setSize(int size){};
    virtual uint64_t *          removeUFDFromLRU(int ufd, int *numPages){};
    virtual void                printLRUBuffer(FILE * file=NULL){};

protected:
    LRUBuffer(){};
    LRUBuffer(const LRUBuffer &o);
    const LRUBuffer & operator =(const LRUBuffer &o);
};
#endif
