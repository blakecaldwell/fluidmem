/*
 * Copyright 2016 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, Sep 2016
 */

/*
 * externRAMClientImpl.hh
 *
 * This defines the implementation class of the interface externRAMClient
 * that deals specifically with a C++ unordered_map
*/


#ifndef _EXTERNRAMCLIENTIMPL_H_
#define _EXTERNRAMCLIENTIMPL_H_
#include <externRAMClient.hh>
#include <sys/user.h>
#include <unordered_map>

using namespace std;

// we use this to replace some detructors.
struct null_deleter
{
    void operator()(void const *) const
    {
    }
};

class externRAMClientImpl: public externRAMClient
{
  typedef uint8_t * page_data;
  typedef unordered_map<uint64_t, page_data> kvStore;

private:
  kvStore kv;
  pthread_mutex_t noop_mutex;

public:
  virtual ~externRAMClientImpl();
  externRAMClientImpl();

  virtual void *      write(uint64_t key, void **data, int size, int * err);
  // get returns size of value retrieved
  virtual int         read(uint64_t key, void ** value);
  virtual int         multiRead(uint64_t * hashcodes, int num_prefetch, void ** recvBufs, int * lengths);
  virtual bool        multiWrite(uint64_t * hashcodes, int num_write, void ** data, int * lengths, int * err);
#ifdef ASYNREAD
  virtual void        read_top(uint64_t key, void ** value);
  virtual int         read_bottom(uint64_t key, void ** value);
  virtual void        multiRead_top(uint64_t * hashcodes, int num_prefetch, void ** recvBufs, int * lengths);
  virtual int         multiRead_bottom(uint64_t * hashcodes, int num_prefetch, void ** recvBufs, int * lengths);
#endif
  virtual int         remove(uint64_t key);

};
#endif
