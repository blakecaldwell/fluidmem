/*
 * Copyright 2015 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, May 2015
 */

/*
 * externRAMClientImpl.hh
 *
 * This defines the implementation class of the interface externRAMClient
 * that deals specifically with memcached
*/


#ifndef _EXTERNRAMCLIENTIMPL_H_
#define _EXTERNRAMCLIENTIMPL_H_
#include <externRAMClient.hh>
#include <boost/shared_ptr.hpp>
#include <libmemcached/memcached.hpp>
#include <libmemcachedutil-1.0/pool.h>
#include <libmemcached-1.0/strerror.h>

using namespace memcache;
#define LOOKAHEAD_SIZE 4
#define KEYSTR_LEN 12

// we use this to replace some detructors.
struct null_deleter
{
    void operator()(void const *) const
    {
    }
};


class externRAMClientImpl: public externRAMClient
{
private:
    // Points to the instance of a client connection to memcached
    memcached_st * myClient;
#ifdef THREADED_WRITE_TO_EXTERNRAM
    memcached_st * myClient_write;
#endif
#ifdef THREADED_PREFETCH
    memcached_st * myClient_multiread;
#endif
    void buildHashStrings(uint64_t * hashcodes, int num_prefetch);
    char ** hashcodeStrings;
    size_t *key_lengths;

public:
    virtual ~externRAMClientImpl();
    externRAMClientImpl();

    virtual void*       write(uint64_t hashcode, void **data, int size, int *err);
    virtual int         read(uint64_t hashcode, void ** recvBuf);
    virtual int         multiRead(uint64_t * hashcodes, int num_prefetch, void ** recvBufs, int * lengths);
    virtual void        multiReadTest();
    virtual bool        multiWrite(uint64_t * hashcodes, int num_write, void ** data, int * lengths, int *err);
    virtual void        read_top(uint64_t key, void ** value);
    virtual int         read_bottom(uint64_t key, void ** value);
    virtual void        multiRead_top(uint64_t * hashcodes, int num_prefetch, void ** recvBufs, int * lengths);
    virtual int         multiRead_bottom(uint64_t * hashcodes, int num_prefetch, void ** recvBufs, int * lengths);
    virtual int         remove(uint64_t hashcode);
};
#endif
