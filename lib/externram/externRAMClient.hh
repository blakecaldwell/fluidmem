/*
 * Copyright 2015 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, May 2015
 */

/*
 * externRAMClient.hh
 * This defines the abstract interface for interacting with externalized RAM
 * clients such as RAMCloud
 */

#ifndef EXTERNRAMCLIENT_HH
#define EXTERNRAMCLIENT_HH
#include <stdint.h>
#include "../../include/usage.h"
#include <iostream>

#define NUM_ERRORS_TO_CHECK_ISFULL 10

// Abstract interface for extern RAM client.
class externRAMClient
{
public:
    virtual         ~externRAMClient(){};

    // Pimpl pattern
    static externRAMClient *        create(int impl_type, char * config, uint64_t upid);

    // API with clients of externRAMClient
    virtual int         write(uint64_t, void *, int ){};
    virtual int         multiWrite(uint64_t *,int,void **,int *){};
    virtual int         read(uint64_t, void *){return 0;};
    virtual int         multiRead(uint64_t *,int,void **,int *){};
#ifdef ASYNREAD
    virtual void        read_top(uint64_t, void *){};
    virtual int         read_bottom(uint64_t, void *){};
    virtual void        multiRead_top(uint64_t *,int,void **,int *){};
    virtual int         multiRead_bottom(uint64_t *,int,void **,int *){};
#endif
    virtual bool        isFull(uint64_t){return false;};
    virtual bool        isFullAll(){return false;};
    virtual int         getUsage(ServerUsage**){};

//protected:
    externRAMClient(){};
    externRAMClient(const externRAMClient &o);
    const externRAMClient & operator =(const externRAMClient &o);
};
#endif
