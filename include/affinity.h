#ifndef __affinity_h__
#define __affinity_h__

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <pthread.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <sched.h>
#include <errno.h>
#include <unistd.h>

#ifdef HWLOC
// not implememted yet

#include <hwloc.h>
hwloc_topology_t topology;
#endif

#include <dbg.h>
/* define the CPU used for each thread */
/* relative value to the default CPU */
#define CPU_FOR_POLLING_THREAD 1
#define CPU_FOR_REINIT_READPAGE_THREAD 2
#define CPU_FOR_REINIT_EVICTPAGE_THREAD 2
#define CPU_FOR_REAPER_THREAD 2
#define CPU_FOR_UI_THREAD 2
#define CPU_FOR_MAIN_THREAD 2
#define CPU_FOR_NEW_UFD_HANDLER_THREAD 2
#define CPU_FOR_WRITE_THREAD 3
#define CPU_FOR_PREFETCH_THREAD 4

#define handle_error_en(en, msg) \
	do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

cpu_set_t container_cpuset;

static inline void setAffinity(int start, int end)
{
	int s, j;
	cpu_set_t cpuset;
	pthread_t thread;

	thread = pthread_self();

	CPU_ZERO(&cpuset);
	for (j = start; j < end; j++)
		CPU_SET(j, &cpuset);

	s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	if (s != 0)
		handle_error_en(s, "pthread_setaffinity_np");

	/* Check the actual affinity mask assigned to the thread */

	s = pthread_getaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	if (s != 0)
		handle_error_en(s, "pthread_getaffinity_np");

	log_debug("%s: Set returned by pthread_getaffinity_np() contained:", __func__);
	for (j = 0; j < CPU_SETSIZE; j++) {
		if (CPU_ISSET(j, &cpuset)) {
			log_debug("%s:    CPU %d", __func__, j);
		}
	}
}

static inline int getNthInCpuSet(int n)
{
	const int nCores = sysconf( _SC_NPROCESSORS_ONLN );
	int i = 0, count = 0;

        if (n == 0) {
		handle_error_en(errno, "can't get the 0th cpu");
	}

	for (i = 0; i < nCores; i++) {
		if (CPU_ISSET(i, &container_cpuset)) {
                        count++;
        	}
		if (count >= n)
			return i;
	}

	return i;
}

static inline void setThreadCPUAffinity(int cpu_index, char * threadname, int tid)
{
#ifdef ENABLE_AFFINITY

#ifdef HWLOC
        int numa_nodes_per_machine = hwloc_get_nbobjs_by_depth(topology, 1);
        int cores_per_numa_node = hwloc_get_nbobjs_by_depth(topology, 2);
#else
        int cpu = getNthInCpuSet(cpu_index);
	log_debug("%s: setting the CPU affinity of thread '%s' to %d. thread id : %d", __func__, threadname, cpu, tid);
	setAffinity(cpu, cpu+1);
#endif
#endif
}

static inline void initTopology()
{
#ifdef HWLOC
	hwloc_topology_init(&topology);  // initialization
	hwloc_topology_load(topology);   // actual detection
#else
	CPU_ZERO(&container_cpuset);
	int s = sched_getaffinity(getpid(), sizeof(cpu_set_t), &container_cpuset);
	if (s != 0) {
		handle_error_en(s, "sched_getaffinity");
	}
#endif
}

static inline void destroyTopology()

{
#ifdef HWLOC
    hwloc_topology_destroy(topology);
#endif
}


#endif
