/*
 * Copyright 2016 William Mortl, University of Colorado,  All Rights Reserved
 * Copyright 2016 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by William Mortl <william.mortl@colorado.edu>
 */

#ifndef MONITORSTATS_H
#define MONITORSTATS_H

/*
 *
 * Includes
 *
 */
#include <stdint.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

/*
 *
 * Enums, typedefs
 *
 */
typedef enum _StatisticToRetreive
{
    CACHE_HIT,
    CACHE_MISS,
    PAGE_FAULTS,
    PLACED_PAGES,
    PAGES_EVICTED,
    ZEROPAGES,
    CACHE_HITRATIO,
    LRU_BUFFERSIZE,
    LRU_BUFFERCAP,
    NUM_UFDS,
    WRITES_AVOIDED,
    WRITES_SKIPPED_ZERO,
    WRITES_SKIPPED_INVALID
} StatisticToRetreive;


typedef struct _MonitorStats
{
    // updated by main handler thread
    unsigned long total_pagefault_count;
    unsigned long placed_data_pages_count;
    unsigned long page_eviction_count;
    unsigned long writes_avoided;
    unsigned long writes_skipped_zero;
    unsigned long writes_skipped_invalid;
    unsigned long page_cache_hits_count;
    unsigned long page_cache_miss_count;
    char pad_0[40];
    /* inserted to place the other items on a separate cache line */

    // updated by main_thread, ui_processing threaad, and reaperthread
    unsigned long LRU_Buffer_size;
    unsigned long LRU_Buffer_capacity;

    // updated by main_thread, new_ufd_handler, polling_thread (remove)
    int registered_UFD_count;

    int last_fault_count;
    struct timeval last_count_time;
    struct timeval last_page_fault;

    pthread_mutex_t last_page_fault_lock;
    pthread_mutex_t LRU_Buffer_size_lock;
    pthread_mutex_t LRU_Buffer_capacity_lock;
    pthread_mutex_t registered_UFD_count_lock;
} MonitorStats;

/*
 *
 * Globals
 *
 */

extern MonitorStats* _pstats;

/*
 *
 * Prototypes
 *
 */

inline void StatsDestroy();
inline void StatsClear();
inline void MonitorStatsInit();

// functions to increment counters
static inline void StatsIncrCacheHit_notlocked()
{
        // increment cache hit
        _pstats->page_cache_hits_count++;
}

static inline void StatsIncrCacheMiss_notlocked()
{
        // increment cache miss
        _pstats->page_cache_miss_count++;
}

static inline void StatsIncrWriteAvoided_notlocked()
{
        // increment writes avoided
        _pstats->writes_avoided++;
}

static inline void StatsIncrWriteSkippedZero_notlocked()
{
        // increment writes avoided
        _pstats->writes_skipped_zero++;
}

static inline void StatsIncrWriteSkippedInvalid_notlocked()
{
        // increment writes avoided
        _pstats->writes_skipped_invalid++;
}

static inline void StatsIncrLRUBufferSize()
{
        pthread_mutex_lock(&_pstats->LRU_Buffer_size_lock);
        _pstats->LRU_Buffer_size++;
        pthread_mutex_unlock(&_pstats->LRU_Buffer_size_lock);
}

static inline void StatsDecrLRUBufferSize()
{
        pthread_mutex_lock(&_pstats->LRU_Buffer_size_lock);
        _pstats->LRU_Buffer_size--;
        pthread_mutex_unlock(&_pstats->LRU_Buffer_size_lock);
}

static inline void StatsSetLRUBufferCap(unsigned long bufferCap)
{
        pthread_mutex_lock(&_pstats->LRU_Buffer_capacity_lock);
        _pstats->LRU_Buffer_capacity = bufferCap;
        pthread_mutex_unlock(&_pstats->LRU_Buffer_capacity_lock);
}

static inline void StatsSetLRUBufferSize(unsigned long bufferSize)
{
        pthread_mutex_lock(&_pstats->LRU_Buffer_size_lock);
        _pstats->LRU_Buffer_size = bufferSize;
        pthread_mutex_unlock(&_pstats->LRU_Buffer_size_lock);
}

static inline void StatsInitLocks()
{
        log_trace_in("%s", __func__);

        pthread_mutex_init(&_pstats->last_page_fault_lock, NULL);
        pthread_mutex_init(&_pstats->LRU_Buffer_size_lock, NULL);
        pthread_mutex_init(&_pstats->LRU_Buffer_capacity_lock, NULL);
        pthread_mutex_init(&_pstats->registered_UFD_count_lock, NULL);

        log_trace_out("%s", __func__);
}

static inline void StatsDestroyLocks()
{
        log_trace_in("%s", __func__);

        if (_pstats) {
          pthread_mutex_destroy(&_pstats->LRU_Buffer_size_lock);
          pthread_mutex_destroy(&_pstats->LRU_Buffer_capacity_lock);
          pthread_mutex_destroy(&_pstats->registered_UFD_count_lock);
        }

        log_trace_out("%s", __func__);
}

static inline unsigned long StatsGetCacheHit_notlocked()
{
        return _pstats->page_cache_hits_count;
}

static inline unsigned long StatsGetCacheMiss_notlocked()
{
        return _pstats->page_cache_miss_count;
}

static inline unsigned long StatsGetPageFault_notlocked()
{
        return _pstats->total_pagefault_count;
}

static inline unsigned long StatsGetPlacedPage_notlocked()
{
        return _pstats->placed_data_pages_count;
}

static inline unsigned long StatsGetPageEvicted_notlocked()
{
        return _pstats->page_eviction_count;
}

static inline unsigned long StatsGetWriteAvoided_notlocked()
{
        return _pstats->writes_avoided;
}

static inline unsigned long StatsGetWriteSkippedZero_notlocked()
{
        return _pstats->writes_skipped_zero;
}

static inline unsigned long StatsGetWriteSkippedInvalid_notlocked()
{
        return _pstats->writes_skipped_invalid;
}

static inline unsigned long StatsGetLRUBufferSize()
{
        unsigned long ret;

        pthread_mutex_lock(&_pstats->LRU_Buffer_size_lock);
        ret = _pstats->LRU_Buffer_size;
        pthread_mutex_unlock(&_pstats->LRU_Buffer_size_lock);

        return ret;
}

static inline unsigned long StatsGetLRUBufferCap()
{
        unsigned long ret;

        pthread_mutex_lock(&_pstats->LRU_Buffer_capacity_lock);
        ret = _pstats->LRU_Buffer_capacity;
        pthread_mutex_unlock(&_pstats->LRU_Buffer_capacity_lock);

        return ret;
}

static inline unsigned long StatsGetNumUFDS()
{
        unsigned long ret;

        pthread_mutex_lock(&_pstats->registered_UFD_count_lock);
        ret = _pstats->registered_UFD_count;
        pthread_mutex_unlock(&_pstats->registered_UFD_count_lock);

        return ret;
}

static inline unsigned long _StatsGetLastFaultCount()
{
        return _pstats->last_fault_count;
}

static inline void _StatsSetLastFaultCount()
{
        _pstats->last_fault_count = StatsGetPageFault_notlocked();
}

static inline double _StatsGetTimeDiff()
{
        struct timeval curr, diff;
        gettimeofday(&curr, NULL);
        timersub(&curr, &_pstats->last_count_time, &diff);

        return (double)diff.tv_sec + ((double)diff.tv_usec / 1000000.0);
}

static inline double _StatsSetLastTime()
{
        gettimeofday(&_pstats->last_count_time, NULL);
}

static inline double StatsSetLastFaultTime()
{
        pthread_mutex_lock(&_pstats->last_page_fault_lock);
        gettimeofday(&_pstats->last_page_fault, NULL);
        pthread_mutex_unlock(&_pstats->last_page_fault_lock);
}

static inline unsigned long StatsGetStat(StatisticToRetreive statToRetrieve)
{
        // var init
        unsigned long ret = 0;

        // figure out what to retrieve
        switch(statToRetrieve)
        {
                case CACHE_HIT:
                {
                        ret = StatsGetCacheHit_notlocked();
                        break;
                }
                case CACHE_MISS:
                {
                        ret = StatsGetCacheMiss_notlocked();
                        break;
                }
                case PAGE_FAULTS:
                {
                        ret = StatsGetPageFault_notlocked();
                        break;
                }
                case PLACED_PAGES:
                {
                        ret = StatsGetPlacedPage_notlocked();
                        break;
                }
                case PAGES_EVICTED:
                {
                        ret = StatsGetPageEvicted_notlocked();
                        break;
                }
                case ZEROPAGES:
                {
                        ret = StatsGetPageFault_notlocked() - StatsGetPlacedPage_notlocked();
                        break;
                }
                case CACHE_HITRATIO:
                {
                        unsigned long hits = StatsGetCacheHit_notlocked();
                        unsigned long total = hits + StatsGetCacheMiss_notlocked();
                        if (total != 0)
                        {
                                ret = ceil(100 * ((double)(hits)/(double)(total)));
                        }
                        break;
                }
                case LRU_BUFFERSIZE:
                {
                        ret = StatsGetLRUBufferSize();
                        break;
                }
                case LRU_BUFFERCAP:
                {
                        ret = StatsGetLRUBufferCap();
                        break;
                }
                case NUM_UFDS:
                {
                        ret = StatsGetNumUFDS();
                        break;
                }
                case WRITES_AVOIDED:
                {
                        ret = StatsGetWriteAvoided_notlocked();
                        break;
                }
                case WRITES_SKIPPED_ZERO:
                {
                        ret = StatsGetWriteSkippedZero_notlocked();
                        break;
                }
                case WRITES_SKIPPED_INVALID:
                {
                        ret = StatsGetWriteSkippedInvalid_notlocked();
                        break;
                }
        }

        return ret;
}

static inline double StatsGetRate()
{
        double ret;
        unsigned long fault_diff = StatsGetPageFault_notlocked() - _StatsGetLastFaultCount();
        ret = ((double) fault_diff / _StatsGetTimeDiff());
        _StatsSetLastFaultCount();
        _StatsSetLastTime();
        return ret;
}

static inline struct timeval _StatsGetLastFaultTime()
{
        struct timeval ret;

        pthread_mutex_lock(&_pstats->last_page_fault_lock);
        ret = _pstats->last_page_fault;
        pthread_mutex_unlock(&_pstats->last_page_fault_lock);

        return ret;
}

static struct timeval StatsGetLastFaultTime()
{
        struct timeval ret;
	ret = _StatsGetLastFaultTime();
        return ret;
}

static inline void StatsSetNumUFDS(unsigned long numUFDS)
{
        pthread_mutex_lock(&_pstats->registered_UFD_count_lock);
        _pstats->registered_UFD_count = numUFDS;
        pthread_mutex_unlock(&_pstats->registered_UFD_count_lock);
}

static inline void StatsIncrPlacedPage_notlocked()
{
        // increment placed page
        _pstats->placed_data_pages_count++;
}

static inline void StatsIncrPageFault_notlocked()
{
        // increment page fault
        _pstats->total_pagefault_count++;
}

static inline void StatsIncrPageEvicted_notlocked()
{
        // increment page eviction
        _pstats->page_eviction_count++;
}
#endif
