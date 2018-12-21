/*
 * Copyright 2017 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, Jan 2017
 */

#ifndef _USAGE_H
#define _USAGE_H
#include <stdint.h>

#ifdef __cplusplus
extern "C"
#endif
typedef struct _ServerUsage {
    uint64_t size;
    uint64_t used;
    uint64_t free;
} ServerUsage;
#endif
