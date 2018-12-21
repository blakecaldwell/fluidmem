/*
 * Copyright 2015 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, May 2015
 */

/****
 *
 * The purpose of libuserfault is to provide an API for applications to use
 * to make use of the Linux kernel userfault capability. Initially , the only
 * use case supported is remote memory.
 *
 ****/
#ifndef UI_PROCESSING_H
#define UI_PROCESSING_H

void *ui_processing_thread(void * tmp); 
#endif
