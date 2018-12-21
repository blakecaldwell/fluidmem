/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __zookeeper_upid_h__
#define __zookeeper_upid_h__

#include <dbg.h>
#include <zookeeper/zookeeper.h>
#include <stdlib.h>
#include <stdio.h>

#include <unistd.h>

#include <stdint.h>
#include <math.h>

#include <dbg.h>

#define COUNT 100
#define ZOOKEEPER_UPID_OK 0
#define ZOOKEEPER_UPID_EXIST 1
#define ZOOKEEPER_UPID_ERR 2

static zhandle_t *zh=NULL;
static clientid_t myid;
static int shutdownThisThing=0;

void initialize()
{
    log_trace_in("%s", __func__);

    zh = NULL;
    memset( &myid,0,sizeof(myid) );
    shutdownThisThing = 0;

    log_trace_out("%s", __func__);
}

void watcher(zhandle_t *zzh, int type, int state, const char *path,
             void* context)
{
    log_trace_in("%s", __func__);

    if (type == ZOO_SESSION_EVENT) {
        if (state == ZOO_CONNECTED_STATE) {
            const clientid_t *id = zoo_client_id(zzh);
            if (myid.client_id == 0 || myid.client_id != id->client_id) {
                myid = *id;
                log_debug("%s: got a new session id: 0x%llx", __func__,
                          (long long) myid.client_id);
           }
        } else if (state == ZOO_AUTH_FAILED_STATE) {
            log_err("Authentication failure. Shutting down.", __func__);
            zookeeper_close(zzh);
            shutdownThisThing=1;
            zh=0;
        } else if (state == ZOO_EXPIRED_SESSION_STATE) {
            log_err("Session expired. Shutting down.", __func__);
            zookeeper_close(zzh);
            shutdownThisThing=1;
            zh=0;
        }
    }

    log_trace_out("%s", __func__);
}

int add_upid(const char *hp, uint64_t upid) {
    log_trace_in("%s", __func__);

    int attemptCnt=10;
    char buffer[512];
    struct Stat stat;
    int len = 512;
    int rc = 0;
    const char * upidPath = "/upids";
    char upidFullPath[1024];
    initialize();

    sprintf( upidFullPath, "%s/%lx", upidPath, upid );
    while(attemptCnt-->0)
    {
        if( zh==NULL )
        {
            zoo_set_debug_level(ZOO_LOG_LEVEL_ERROR);
            zh = zookeeper_init(hp, watcher, 30000, &myid, 0, 0);
            if( zh==NULL )
                goto add_reconnect;
        }
        while(!shutdownThisThing) {
            rc = zoo_exists(zh, upidPath, 0, &stat);
            if( rc==ZNONODE )
            {
                rc = zoo_create(zh, upidPath, NULL, -1,
                        &ZOO_OPEN_ACL_UNSAFE, 0, 0, 0);
                if( rc!=ZOK )
                    goto add_retry;
            } else if( rc!=ZOK )
                goto add_retry;

            rc = zoo_exists(zh, upidFullPath, 0, &stat);
            if( rc==ZOK )
            {
                zookeeper_close(zh);
                log_trace_out("%s", __func__);
                return ZOOKEEPER_UPID_EXIST;
            } else if( rc==ZNONODE )
            {
                rc = zoo_create(zh, upidFullPath, NULL, -1,
                        &ZOO_OPEN_ACL_UNSAFE, 0, 0, 0);
                if( rc==ZOK )
                {
                    zookeeper_close(zh);
                    log_trace_out("%s", __func__);
                    return ZOOKEEPER_UPID_OK;
                }
            }
add_retry:
            log_debug("%s: retrying", __func__);
            sleep(1+pow(2,10-attemptCnt));
        }
add_reconnect:
        log_debug("%s: reconnecting", __func__);
        sleep(1+pow(2,10-attemptCnt));
    }
    if( zh!=NULL )
        zookeeper_close(zh);

    log_trace_out("%s", __func__);
    return ZOOKEEPER_UPID_ERR;
}

int del_upid(const char * hp, uint64_t upid) {
    log_trace_in("%s", __func__);

    int attemptCnt=10;
    char buffer[512];
    struct Stat stat;
    int len = 512;
    int rc = 0;
    const char * upidPath = "/upids";
    char upidFullPath[1024];
    initialize();

    sprintf( upidFullPath, "%s/%lx", upidPath, upid );

    while(attemptCnt-->0)
    {
        if( zh==NULL )
        {
            zoo_set_debug_level(ZOO_LOG_LEVEL_ERROR);
            zh = zookeeper_init(hp, watcher, 30000, &myid, 0, 0);
            if( zh==NULL )
                goto del_reconnect;
        }
        while(!shutdownThisThing) {
            rc = zoo_exists(zh, upidFullPath, 0, &stat);
            if( rc==ZNONODE )
            {
                zookeeper_close(zh);
                log_trace_out("%s", __func__);
                return ZOOKEEPER_UPID_ERR;
            } else if( rc!=ZOK )
                goto del_retry;

            rc = zoo_delete(zh, upidFullPath, -1);
            if( rc==ZOK )
            {
                zookeeper_close(zh);
                log_trace_out("%s", __func__);
                return ZOOKEEPER_UPID_OK;
            }
del_retry:
            log_debug("%s: retrying", __func__);
            sleep(1+pow(2,10-attemptCnt));
        }
del_reconnect:
        log_debug("%s: reconnecting", __func__);
        sleep(1+pow(2,10-attemptCnt));
    }
    if( zh!=NULL )
        zookeeper_close(zh);

    log_trace_out("%s", __func__);
    return ZOOKEEPER_UPID_ERR;
}

#endif // __zookeeper_upid_h__
