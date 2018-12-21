/*
 * Copyright 2016 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, January 2016
 */

/****
 *
 * The purpose of libuserfault is to provide an API for applications to use
 * to make use of the Linux kernel userfault capability. Initially, the only
 * use case supported is remote memory.
 *
 ****/
#ifndef USERFAULTCLIENT_H
#define USERFAULTCLIENT_H
#include <linux/userfaultfd.h>
#include <stdint.h>      /* for uint64_t */
#include <stdbool.h>

/* Client API for sending FD's */
char * get_home_socket_path(void);
int connect_monitor(char * socket_path);
int send_fd(int socket, int fd_to_send);
void * allocate_userfault(int *ufd, uint64_t size);

/* internal implementation functions */
int ufd_syscall(void);

/* Used for handshake protocol before using a ufd */
bool ufd_version_check(int ufd);

/* Mark the already allocated region with VM_USERFAULT in the VMA
 * "arms" the userfault region
 */
int enable_ufd_area(int * ufd, void * area, uint64_t size);

/* Unregister the memory area. The ufd is no longer valid after this */
int disable_ufd_area(int ufd, void * area, uint64_t size);

#endif
