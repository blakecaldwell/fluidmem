/* Copyright 2015 Daniel Zurawski, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, May 2015
 */

#include "pollfd_vector.h"
#include <dbg.h>

#ifdef MONITORSTATS
#include <monitorstats.h>
#endif

int pollfd_vector_init(Pollfd_vector *vector) {
	log_trace_in("%s", __func__);

	int ret = -1;

	if (pthread_mutex_init(&vector->remove_fd_lock, NULL) != 0) {
		ret = -1;
	}

	if (pthread_mutex_init(&vector->add_fd_lock, NULL) != 0) {
		ret = -1;
	}

	vector->size = 0;
	vector->capacity = VECTOR_INITIAL_CAPACITY;

	vector->list = malloc(sizeof(struct pollfd) * vector->capacity);

	if (vector->list)
		ret = 0;

#ifdef MONITORSTATS
	StatsSetNumUFDS(0);
#endif

	log_trace_out("%s", __func__);
	return ret;
}

void pollfd_vector_append(Pollfd_vector *vector, struct pollfd fd) {
	log_trace_in("%s", __func__);

	if (vector->size == vector->capacity) {
		pollfd_vector_double(vector);
	}

	vector->size += 1;
#ifdef MONITORSTATS
	// decrement by 2 for add and remove reload fds
	StatsSetNumUFDS(vector->size - 2);
#endif
	vector->list[vector->size - 1] = fd;

	log_trace_out("%s", __func__);
}

void pollfd_vector_compact(Pollfd_vector *vector) {
	log_trace_in("%s", __func__);

	if ((vector->capacity > VECTOR_INITIAL_CAPACITY) &&
		(vector->size < (vector->capacity / 2))) {
		vector->capacity /= 2;
		vector->list = realloc(vector->list, sizeof(struct pollfd) * vector->capacity);
	}

	log_trace_out("%s", __func__);
}

int pollfd_vector_remove(Pollfd_vector *vector, int fd) {
	log_trace_in("%s", __func__);

	int ret = -1;
	int i;

	// find index of matching fd
	for (i = 0; i < vector->size; i++) {
		if (vector->list[i].fd == fd) {
			// replace struct pollfd entry with the last one on the list
			vector->list[i] = vector->list[vector->size - 1];
			vector->size -= 1;
#ifdef MONITORSTATS
			// decrement by 2 for add and remove reload fds
			StatsSetNumUFDS(vector->size - 2);
#endif
			// compact pollfd vector with reduced size
			pollfd_vector_compact(vector);
			ret = 0;
			break;
		}
	}

	log_trace_out("%s", __func__);
	return ret;
}

void pollfd_vector_double(Pollfd_vector *vector) {
	log_trace_in("%s", __func__);

	vector->capacity *= 2;
	vector->list = realloc(vector->list, sizeof(struct pollfd) * vector->capacity);

	log_trace_out("%s", __func__);
}

void pollfd_vector_close(Pollfd_vector *vector) {
	log_trace_in("%s", __func__);

	log_warn("%s: closing %d userfault fd's", __func__, vector->size - 2);

	// close fds with index 0 (reload_fd) and 1 (add_fd) as well
	int i;
	for (i = 0; i < vector->size; i++) {
		close(vector->list[i].fd);
	}

	log_trace_out("%s", __func__);
}

void pollfd_vector_free(Pollfd_vector *vector) {
	log_trace_in("%s", __func__);

	pthread_mutex_destroy(&vector->remove_fd_lock);
	pthread_mutex_destroy(&vector->add_fd_lock);
	free(vector->list);

	log_trace_out("%s", __func__);
}

void add_fd (Pollfd_vector *vector, int new_fd) {
	log_trace_in("%s", __func__);

	struct pollfd new_pollfd;
	new_pollfd.fd = new_fd;
	new_pollfd.events = POLLIN;
	new_pollfd.revents = 0;
	pollfd_vector_append(vector, new_pollfd);
	log_debug("%s: new size of pollfd_vector is %d out of %d", __func__, vector->size, vector->capacity);

	log_trace_out("%s", __func__);
}

int del_fd (Pollfd_vector *vector, int fd) {
	log_trace_in("%s", __func__);

	int ret;

	if (pollfd_vector_remove(vector, fd) == 0) {
		log_debug("%s: removed fd %d, new size of pollfd_vector is %d out of %d", __func__, fd, vector->size, vector->capacity);
		ret = 0;
	}
	else {
		log_warn("%s: failed to find fd %d for removal from pollfd_vector", __func__, fd);
		ret = -1;
	}

	log_trace_out("%s", __func__);
	return ret;
}
