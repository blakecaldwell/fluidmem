#ifndef __POLLFD_VECTOR_H__
#define __POLLFD_VECTOR_H__

#include <poll.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>

#define VECTOR_INITIAL_CAPACITY 50

typedef struct {
	int size;
	int capacity;
	int reload_addfd;
	int reload_removefd;
	pthread_mutex_t remove_fd_lock;
	pthread_mutex_t add_fd_lock;
	struct pollfd *list;
} Pollfd_vector;

int pollfd_vector_init(Pollfd_vector *vector);
void pollfd_vector_append(Pollfd_vector *vector, struct pollfd fd);
int pollfd_vector_remove(Pollfd_vector *vector, int fd);
void pollfd_vector_double(Pollfd_vector *vector);
void pollfd_vector_compact(Pollfd_vector *vector);
void pollfd_vector_close(Pollfd_vector *vector);
void pollfd_vector_free(Pollfd_vector *vector);
void add_fd (Pollfd_vector *vector, int new_fd);
int del_fd (Pollfd_vector *vector, int fd);

#endif
