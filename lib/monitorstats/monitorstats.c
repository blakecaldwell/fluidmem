/*
 * Copyright 2016 William Mortl, University of Colorado,  All Rights Reserved
 * Copyright 2016 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by William Mortl <william.mortl@colorado.edu>
 */

/*
 *
 * Includes
 *
 */
#include <stdlib.h>
#include <string.h>
#include <dbg.h>
#include "monitorstats.h"
#ifdef TIMING
#include "timingstats.h"
#endif

/*
 *
 * Function implementations
 *
 */

inline void StatsDestroy()
{
        log_trace_in("%s", __func__);

        int i;

        StatsDestroyLocks();
        if (_pstats)
                free(_pstats);
#ifdef TIMING
        DestroyBuckets();
#endif

        log_trace_out("%s", __func__);
}

// call to init this library of functions
inline void MonitorStatsInit()
{
        log_trace_in("%s", __func__);

        _pstats = (MonitorStats *) malloc(sizeof(MonitorStats));

        StatsInitLocks();
        StatsClear();

        log_trace_out("%s", __func__);
}

inline void StatsClear()
{
        _pstats->total_pagefault_count = 0;
        _pstats->placed_data_pages_count = 0;
        _pstats->page_eviction_count = 0;
        _pstats->writes_avoided = 0;
        _pstats->page_cache_hits_count = 0;
        _pstats->page_cache_miss_count = 0;
        _pstats->last_fault_count = 0;
	_pstats->last_page_fault = (struct timeval){ 0 };
        _pstats->writes_skipped_zero = 0;
        _pstats->writes_skipped_invalid = 0;
        _StatsSetLastTime();

#ifdef TIMING
        StatsClearBuckets();
#endif
}
