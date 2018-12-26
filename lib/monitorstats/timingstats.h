/*
 * Copyright 2017 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <caldweba@colorado.edu>
 */

#ifndef TIMINGSTATS_H
#define TIMINGSTATS_H

#define NUM_BUCKETS 26
#define MAX_BUCKET_SLOTS 8192

/*
 *
 * Includes
 *
 */
#include <dbg.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>

typedef enum _BucketType
{
  HANDLE_USERFAULT_COPY,       // 0x0000001
  HANDLE_USERFAULT_COPY_EVICT, // 0x0000002
  HANDLE_USERFAULT_ZERO,       // 0x0000004
  HANDLE_USERFAULT_ZERO_EVICT, // 0x0000008
  HANDLE_USERFAULT_MOVE,       // 0x0000010
  HANDLE_USERFAULT_MOVE_EVICT, // 0x0000020
  HANDLE_USERFAULT_ASYN_EVICT, // 0x0000040
  HANDLE_USERFAULT_ALL,        // 0x0000080
  EVICT_TO_EXTERNRAM,          // 0x0000100
  READ_FROM_EXTERNRAM,         // 0x0000200
  READ_PAGES,                  // 0x0000400
  READ_PAGE,                   // 0x0000800
  WRITE_PAGES,                 // 0x0001000
  WRITE_PAGE,                  // 0x0002000
  UFFD_ZEROPAGE,               // 0x0004000
  UFFD_COPY,                   // 0x0008000
  UFFD_REMAP,                  // 0x0010000
  INSERT_LRU_CACHE_NODE,       // 0x0020000
  INSERT_PAGE_HASH_NODE,       // 0x0040000
  UPDATE_PAGE_CACHE,           // 0x0080000
  READ_VIA_PAGE_CACHE,         // 0x0100000
  STORE_PAGES_IN_PAGE_CACHE,   // 0x0200000
  ZEROPAGE_COMPARE,            // 0x0400000
  KVREAD,                      // 0x0800000
  KVWRITE,                     // 0x1000000
  KVCOPY,                      // 0x2000000
} BucketType;

typedef struct _TimingBucket
{
  float *values;
  uint64_t num_items;
  pthread_mutex_t bucket_lock;
  uint32_t overflow;
  double last_mean;
} TimingBucket;

int shmids[NUM_BUCKETS+3];
int shms[NUM_BUCKETS+3];
extern uint32_t max_bucket_slots;
extern char ** reverse_buckets;
extern uint32_t buckets_mask;
extern TimingBucket * timing_buckets;

static inline void allocate_and_copy(const char * s, char ** p) {
  *p = (char *)malloc(sizeof(char) * (strlen(s) + 1));
  strcpy((char *) *p, s);
}

static inline void _TimingStatsInitReverseBuckets()
{
  log_trace_in("%s", __func__);

  allocate_and_copy("HANDLE_USERFAULT_COPY", &reverse_buckets[HANDLE_USERFAULT_COPY]);
  allocate_and_copy("HANDLE_USERFAULT_COPY_EVICT", &reverse_buckets[HANDLE_USERFAULT_COPY_EVICT]);
  allocate_and_copy("HANDLE_USERFAULT_ZERO", &reverse_buckets[HANDLE_USERFAULT_ZERO]);
  allocate_and_copy("HANDLE_USERFAULT_ZERO_EVICT", &reverse_buckets[HANDLE_USERFAULT_ZERO_EVICT]);
  allocate_and_copy("HANDLE_USERFAULT_MOVE", &reverse_buckets[HANDLE_USERFAULT_MOVE]);
  allocate_and_copy("HANDLE_USERFAULT_MOVE_EVICT", &reverse_buckets[HANDLE_USERFAULT_MOVE_EVICT]);
  allocate_and_copy("HANDLE_USERFAULT_ASYN_EVICT", &reverse_buckets[HANDLE_USERFAULT_ASYN_EVICT]);
  allocate_and_copy("HANDLE_USERFAULT_ALL", &reverse_buckets[HANDLE_USERFAULT_ALL]);
  allocate_and_copy("EVICT_TO_EXTENRAM", &reverse_buckets[EVICT_TO_EXTERNRAM]);
  allocate_and_copy("READ_FROM_EXTERNRAM", &reverse_buckets[READ_FROM_EXTERNRAM]);
  allocate_and_copy("READ_PAGES", &reverse_buckets[READ_PAGES]);
  allocate_and_copy("READ_PAGE", &reverse_buckets[READ_PAGE]);
  allocate_and_copy("WRITE_PAGES", &reverse_buckets[WRITE_PAGES]);
  allocate_and_copy("WRITE_PAGE", &reverse_buckets[WRITE_PAGE]);
  allocate_and_copy("UFFD_ZEROPAGE", &reverse_buckets[UFFD_ZEROPAGE]);
  allocate_and_copy("UFFD_COPY", &reverse_buckets[UFFD_COPY]);
  allocate_and_copy("UFFD_REMAP", &reverse_buckets[UFFD_REMAP]);
  allocate_and_copy("INSERT_LRU_CACHE_NODE", &reverse_buckets[INSERT_LRU_CACHE_NODE]);
  allocate_and_copy("INSERT_PAGE_HASH_NODE", &reverse_buckets[INSERT_PAGE_HASH_NODE]);
  allocate_and_copy("UPDATE_PAGE_CACHE", &reverse_buckets[UPDATE_PAGE_CACHE]);
  allocate_and_copy("READ_VIA_PAGE_CACHE", &reverse_buckets[READ_VIA_PAGE_CACHE]);
  allocate_and_copy("STORE_PAGES_IN_PAGE_CACHE", &reverse_buckets[STORE_PAGES_IN_PAGE_CACHE]);
  allocate_and_copy("ZEROPAGE_COMPARE", &reverse_buckets[ZEROPAGE_COMPARE]);
  allocate_and_copy("KVREAD", &reverse_buckets[KVREAD]);
  allocate_and_copy("KVWRITE", &reverse_buckets[KVWRITE]);
  allocate_and_copy("KVCOPY", &reverse_buckets[KVCOPY]);
  log_trace_out("%s", __func__);
}

static inline void _InitBucket(BucketType bucket)
{
  pthread_mutex_init(&timing_buckets[bucket].bucket_lock, NULL);
  timing_buckets[bucket].num_items = 0;
  timing_buckets[bucket].overflow = 0;
  timing_buckets[bucket].last_mean = 0;

  int shm_key = ftok("/bin/ls", bucket);
  if (shm_key == -1)
    log_err("%s: ftok", __func__);

  shmids[bucket] = shmget(shm_key, (sizeof(float)*max_bucket_slots), 0644 | IPC_CREAT);
  if (shmids[bucket] == -1)
    log_err("%s: bucket %d shmget", __func__, bucket);

  timing_buckets[bucket].values = (float *)shmat(shmids[bucket], (void *)0, 0);
  if (timing_buckets[bucket].values == (float *)(-1))
    log_err("%s: shmat to values for bucket %d", __func__, bucket);

  // Zero the stats region, since shared memory may persist values
  // across a restart of monitor
  memset((void *)timing_buckets[bucket].values, 0, sizeof(float) * max_bucket_slots);
}

static inline void _TimingStatsInitBuckets() {
  log_trace_in("%s", __func__);

  int i;
  for(i = 0; i < NUM_BUCKETS; i++) {
    if (((1<<i) & buckets_mask) == 0)
      continue;
    _InitBucket((BucketType)i);
  }

  log_trace_out("%s", __func__);
}

static inline void _TimingStatsAttachShm()
{
  log_trace_in("%s", __func__);

  key_t key1 = ftok("/bin/ls", NUM_BUCKETS+1);
  if (key1 == -1)
    log_err("%s: ftok", __func__);

  // timing_buckets
  shmids[NUM_BUCKETS+1] = shmget(key1, sizeof(TimingBucket) * NUM_BUCKETS, 0644 | IPC_CREAT);
  if (shmids[NUM_BUCKETS+1] == -1)
    log_err("%s: shmid %d shmget", __func__, shmids[NUM_BUCKETS+1]);

  timing_buckets = (TimingBucket *)shmat(shmids[NUM_BUCKETS+1], (void *)0, 0);
  if (timing_buckets == (TimingBucket *)(-1))
    log_err("%s: shmat to timing_buckets", __func__);
  memset((void *)timing_buckets, 0, sizeof(TimingBucket) * NUM_BUCKETS);

  // reverse_buckets
  key_t key2 = ftok("/bin/ls", NUM_BUCKETS+2);
  if (key2 == -1)
    log_err("%s: ftok", __func__);

  shmids[NUM_BUCKETS+2] = shmget(key2, sizeof(char *) * NUM_BUCKETS, 0644 | IPC_CREAT);
  if (shmids[NUM_BUCKETS+2] == -1)
    log_err("%s: shmid %d shmget", __func__, shmids[NUM_BUCKETS+2]);
  reverse_buckets = (char**)shmat(shmids[NUM_BUCKETS+2], (void *)0, 0);
  if (reverse_buckets == (char **)(-1))
    log_err("%s: shmat to reverse_buckets", __func__);
  memset((void *)reverse_buckets, 0, sizeof(char *) * NUM_BUCKETS);

  log_trace_out("%s", __func__);
}

static inline void TimingStatsInitGlobals()
{
  log_trace_in("%s", __func__);

  max_bucket_slots = MAX_BUCKET_SLOTS;

  buckets_mask = ((1L<<NUM_BUCKETS) - 1);

  log_trace_out("%s", __func__);
}

static inline void TimingStatsInit()
{
  log_trace_in("%s", __func__);

  _TimingStatsAttachShm();
  _TimingStatsInitReverseBuckets();
  _TimingStatsInitBuckets();

  log_trace_out("%s", __func__);
}

static inline void StatsClearBuckets()
{
  int i;

  for(i = 0; i < NUM_BUCKETS; i++) {
    if (((1<<i) & buckets_mask) == 0)
      continue;
    memset(timing_buckets[i].values, 0, max_bucket_slots);
    timing_buckets[i].num_items = 0;
    timing_buckets[i].last_mean = 0;
    timing_buckets[i].overflow = 0;
  }
}

static inline void destroy_bucket(BucketType bucket)
{
  struct shmid_ds  shmid_ds;

  pthread_mutex_destroy(&timing_buckets[bucket].bucket_lock);
  if (shmdt(timing_buckets[bucket].values) == -1)
    log_warn("%s: shmdt for bucket %d", __func__, bucket);
  if (shmctl(shmids[(int)bucket], IPC_RMID, &shmid_ds) == -1)
    log_warn("%s: shmctl remove shmid for bucket %d", __func__, bucket);
}

static inline void DestroyBuckets()
{
  int i;
  struct shmid_ds  shmid_ds;

  for(i = 0; i < NUM_BUCKETS; i++) {
    // reverse_buckets string allocated regardless of buckets_mask
    if (reverse_buckets[i])
      free(reverse_buckets[i]);

    if (((1<<i) & buckets_mask) == 0)
      continue;

    destroy_bucket((BucketType)i);
  }
  if (shmdt(timing_buckets) == -1)
    log_warn("%s: shmdt for shmid %d", __func__, shmids[NUM_BUCKETS+1]);
  if (shmctl(shmids[NUM_BUCKETS+1], IPC_RMID, &shmid_ds) == -1)
    log_warn("%s: shmctl remove shmid %d", __func__, shmids[NUM_BUCKETS+1]);
  if (shmdt(reverse_buckets) == -1)
    log_warn("%s: shmdt for shmid %d", __func__, shmids[NUM_BUCKETS+2]);
  if (shmctl(shmids[NUM_BUCKETS+2], IPC_RMID, &shmid_ds) == -1)
    log_warn("%s: shmctl remove shmid %d", __func__, shmids[NUM_BUCKETS+2]);
}

static inline double StatsGetBucketSum(BucketType bucket) {
  int i;
  double sum = 0;

  for(i = 0; i < timing_buckets[bucket].num_items; i++) {
    sum += timing_buckets[bucket].values[i];
  }

  return sum;
}

static inline float StatsGetBucketMean(BucketType bucket) {
  float mean;

  float current_sum = StatsGetBucketSum(bucket);
  int old_num_samples = timing_buckets[bucket].overflow * max_bucket_slots;
  int new_num_samples = old_num_samples + timing_buckets[bucket].num_items;
  mean = (timing_buckets[bucket].last_mean * old_num_samples + current_sum) / new_num_samples;

  return mean;
}

static inline void StatsPrintSingleBucket(BucketType bucket, FILE *out, uint32_t start_item, uint32_t max_item, bool header)
{
  int i;

  if (header)
    fprintf(out, "bucket %s (size=%d): ", reverse_buckets[bucket], max_item);
  for(i = start_item; i < max_item; i++) {
    fprintf(out, "%.2f,", timing_buckets[bucket].values[i]);
  }

  // Need to print overlflow values
  if (timing_buckets[bucket].overflow > 0) {
    for(i = 0; i < start_item; i++) {
      fprintf(out, "%.2f,", timing_buckets[bucket].values[i]);
    }
  }
  fprintf(out, "\n");
}

static inline int cmpfunc(const void *a, const void *b)
{
    return (*(float *) a - *(float *) b);
}


static inline float StatsGetBucketMedian(float *array, int length, float *min, float *max)
{
  float median;

  qsort(array, length, sizeof(float), cmpfunc);

  if (length % 2 == 0) {
    median = (array[(length / 2)-1] + array[length / 2]) / 2;
  } else {
    median = array[((length + 1) / 2)-1];
  }

  *min = array[0];
  *max = array[length-1];
  return median;
}

static inline void store_bucket(BucketType bucket, float value)
{
  log_trace_in("%s",__func__);

  if (((1<<((int)bucket)) & buckets_mask) == 0)
    goto store_bucket_out;

  if (timing_buckets[bucket].num_items == max_bucket_slots) {
    double current_sum = StatsGetBucketSum(bucket);
    uint64_t old_num_samples = timing_buckets[bucket].overflow * max_bucket_slots;
    // detect overflow
    if ((old_num_samples < timing_buckets[bucket].overflow) &&
        (old_num_samples < max_bucket_slots)) {
        log_warn("%s: counter overflow for bucket %s", __func__, reverse_buckets[bucket]);
        old_num_samples = 0;
        timing_buckets[bucket].overflow = 0;
        timing_buckets[bucket].last_mean = 0;
    }
    uint64_t new_num_samples = old_num_samples + timing_buckets[bucket].num_items;
    timing_buckets[bucket].last_mean = (timing_buckets[bucket].last_mean * old_num_samples + current_sum) / new_num_samples;

    timing_buckets[bucket].num_items = 0;
    if (timing_buckets[bucket].overflow++ != UINT32_MAX)
      timing_buckets[bucket].overflow++;
  }

  log_lock("%s: locking timing_buckets[%s].bucket_lock", __func__, reverse_buckets[bucket]);
  pthread_mutex_lock(&timing_buckets[bucket].bucket_lock);
  log_lock("%s: locked timing_buckets[%s].bucket_lock", __func__, reverse_buckets[bucket]);

  timing_buckets[bucket].values[timing_buckets[bucket].num_items] = value;
  timing_buckets[bucket].num_items++;

  log_lock("%s: unlocking timing_buckets[%s].bucket_lock", __func__, reverse_buckets[bucket]);
  pthread_mutex_unlock(&timing_buckets[bucket].bucket_lock);
  log_lock("%s: unlocked timing_buckets[%s].bucket_lock", __func__, reverse_buckets[bucket]);
store_bucket_out:
  log_trace_out("%s",__func__);
}

static inline void StatsPrintBuckets(FILE *out)
{
  log_trace_in("%s", __func__);
  int i;

  for(i = 0; i < NUM_BUCKETS; i++) {
    if (((1<<i) & buckets_mask) == 0)
      continue;

    float median = 0, minTime = 0, maxTime = 0;
    uint32_t start_item = 0, max_item = 0;
    if ((timing_buckets[i].overflow == 0) &&
        (timing_buckets[i].num_items != 0)) {
      max_item = timing_buckets[i].num_items;
    }
    else if (timing_buckets[i].overflow > 0) {
      start_item = timing_buckets[i].num_items;
      max_item = max_bucket_slots;
    }
    else
      continue;
    median = StatsGetBucketMedian(timing_buckets[i].values, max_item, &minTime, &maxTime);
    fprintf(out, "Timing bucket %27s (size=%5d) mean=%4.2f median=%4.2f min=%4.2f max=%4.2f\n",
      reverse_buckets[(BucketType)i], max_item, StatsGetBucketMean((BucketType)i), median, minTime, maxTime);
  }

  log_trace_out("%s", __func__);
}

static inline void StatsDumpBuckets(FILE *out)
{
  log_trace_in("%s", __func__);
  int i;
  int order[NUM_BUCKETS] = { ZEROPAGE_COMPARE, UPDATE_PAGE_CACHE, INSERT_PAGE_HASH_NODE, INSERT_LRU_CACHE_NODE, STORE_PAGES_IN_PAGE_CACHE, UFFD_ZEROPAGE, HANDLE_USERFAULT_ZERO, UFFD_REMAP, UFFD_COPY, READ_PAGE, READ_VIA_PAGE_CACHE, HANDLE_USERFAULT_COPY, HANDLE_USERFAULT_MOVE, WRITE_PAGE, EVICT_TO_EXTERNRAM, READ_FROM_EXTERNRAM, HANDLE_USERFAULT_COPY_EVICT, HANDLE_USERFAULT_MOVE_EVICT, HANDLE_USERFAULT_ASYN_EVICT, READ_PAGES, WRITE_PAGES, KVREAD, KVWRITE, KVCOPY };

  for(i = 0; i < 19; i++) {
    int b = order[i];
    if (((1<<b) & buckets_mask) == 0)
      continue;

    fprintf(out, "'%s',", reverse_buckets[b]);
  }
  fprintf(out, "\n");

  for(i = 0; i < 19; i++) {
    int b = order[i];
    if (((1<<b) & buckets_mask) == 0)
      continue;

    uint32_t start_item = 0, max_item = 0;
    if ((timing_buckets[b].overflow == 0) &&
        (timing_buckets[b].num_items != 0)) {
      max_item = timing_buckets[b].num_items;
    }
    else if (timing_buckets[b].overflow > 0) {
      start_item = timing_buckets[b].num_items;
      max_item = max_bucket_slots;
    }
    else
      continue;

    StatsPrintSingleBucket((BucketType)b, out, start_item, max_item, false);
  }

  log_trace_out("%s", __func__);

}
#endif
