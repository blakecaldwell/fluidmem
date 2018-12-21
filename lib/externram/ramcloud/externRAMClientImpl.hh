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
 * that deals specifically with RAMCloud
*/


#ifndef _EXTERNRAMCLIENTIMPL_H_
#define _EXTERNRAMCLIENTIMPL_H_
#include <externRAMClient.hh>
#include <boost/shared_ptr.hpp>
/*
 * RamCloud.h has to be included after boost includes to avoid the following error:
 *  'boost::BOOST_FOREACH' has not been declared
 */
#include <RamCloud.h>
#include <Context.h>
#include <PerfStats.h>

#define LOOKAHEAD_SIZE 4
#define MAX_NUMBER_OF_RAMCLOUD_SERVERS 20
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
    // Points to the instance of a client connection to RAMCloud
    RAMCloud::RamCloud * myClient;
#ifdef THREADED_WRITE_TO_EXTERNRAM
    RAMCloud::RamCloud * myClient_write;
#endif
#ifdef THREADED_PREFETCH
    RAMCloud::RamCloud * myClient_multiread;
#endif

    // unique id for node
    char clientId[100];

    // table ID in RAMCloud (one per client)
    uint64_t tableId;
#ifdef THREADED_WRITE_TO_EXTERNRAM
    uint64_t tableId_write;
#endif
#ifdef THREADED_PREFETCH
    uint64_t tableId_multiread;
#endif

    // create the table, if it doesn't already exists, the delete it and start fresh
    uint64_t createTable(const char *);

    void dropTable(const char *);

public:
    RAMCloud::Context context;
#ifdef THREADED_WRITE_TO_EXTERNRAM
    RAMCloud::Context context_write;
#endif
#ifdef THREADED_PREFETCH
    RAMCloud::Context context_multiread;
#endif
    virtual ~externRAMClientImpl();
    externRAMClientImpl();

    virtual int         write(uint64_t hashcode, void *data, int size);
    virtual int         read(uint64_t hashcode, void * recvBuf);
    int                 multiRead(uint64_t * hashcodes, int num_prefetch, void ** recvBufs, int * lengths);
    int                 multiWrite(uint64_t * hashcodes, int num_write, void ** data, int * lengths );
    void                multiReadTest();
    int                 remove(uint64_t hashcode);
    void                getPerfStats(RAMCloud::PerfStats* out);
    bool                isFull(uint64_t hashcode);
    bool                isFullAll();
    int                 getUsage(ServerUsage ** usage_ptr);
};
#endif
