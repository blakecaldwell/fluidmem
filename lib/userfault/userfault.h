/*
 * Copyright 2016 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, January 2016
 */

/****
 *
 * The purpose of libuserfault is to provide an API for applications to use
 * to make use of the Linux kernel userfault capability. Initially , the only
 * use case supported is remote memory.
 *
 ****/
#ifndef USERFAULT_H
#define USERFAULT_H
#include <linux/userfaultfd.h>
#include <usage.h>

#include <stdbool.h>
#include <stdint.h>      /* for uint64_t */
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <uthash.h> /* for write_list */

#define MAX_UFDS 100

#define WRITE_FAULT 1
#define READ_FAULT 0
#define FLUSH_TO_EXTERNRAM 0
#define DELETE_FROM_EXTERNRAM 1

#define ZERO_PAGE 0
#define COPY_PAGE 1
#define MOVE_PAGE 2

/*
 * Global variables
 */
char* zeroPage;
char* zookeeperConn;

#ifdef TIMING
int bucket_index;
#endif

/*
 * State used for synchronizing operations modifying the list of fds
 * that are polled
 */
struct poll_state {
  int num_ufds;
  int reload_fd;
  struct pollfd * poll_list;
  pthread_mutex_t mutex;
  pthread_mutex_t handler_mutex;
  sem_t semadd;
  sem_t sempoll;
};

/* Wake the caller after a fault */
int ack_userfault(int ufd, void *start, size_t len);

/* Functions that have effects on pages after region has been registered */
int place_zero_page(int ufd, void * dst);
int place_data_page(int ufd, void * dst, void * src);
int move_page(int ufd, void * dst, void * src);
int evict_if_needed(int ufd, void * dst, int page_type);

int evict_page(int ufd, void * dst, void * src);

/* interface with libexternram */
int evict_to_externram(int ufd, void * pageaddr);
int read_from_externram(int ufd, void * pageaddr);
int evict_to_externram_multi(int size);
int getExternRAMUsage(ServerUsage ** usage);

int resizeLRUBuffer(int size);
int purgeDeadUpids(int ** ufd_list_ptr);
int flush_lru(int ufd, int flush_or_delete);
int flush_buffers(int ufd);
int listPids(uint32_t ** pid_list_ptr);
int removePid(uint32_t pidToRemove);

/* clean up locks */
void clean_up_lock();

/* Monitor API for receiving FD's */
int recv_fd(int socket);
int create_server(char * socket_path);

int setnonblocking(int fd);

#endif
