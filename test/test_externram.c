/*
 * Copyright 2017 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, April 2017
 */

#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/user.h>
#include <unistd.h>

#include <externRAMClientWrapper.h>
#include <dbg.h>
#include <upid.h>
#include <buffer_allocator_array.h>
#include <cpufreq.h>

#include <monitorstats.h>
MonitorStats* _pstats;

#ifdef TIMING
#include <timingstats.h>
uint32_t max_bucket_slots;
char ** reverse_buckets;
uint32_t buckets_mask;
TimingBucket * timing_buckets;
#endif

volatile sig_atomic_t fatal_error_in_progress = 0;
int len;
int ufd;
uint8_t * arr;
uint8_t * shadow;
char * config;
void * tmp_page;

int change_oom_score(int oom_adj) {
  FILE * f = fopen("/proc/self/oom_adj","w");
  if (!f) {
    f = fopen("/proc/self/oom_score_adj","w");
    if (f) {
      fclose(f);
    }
    else {
      log_err("could not set oom_score");
      return 1;
    }
  }
  return 0;
}

int rand_lim(int limit) {
  int divisor = RAND_MAX/(limit+1);
  int retval;

  do {
    retval = rand() / divisor;
  } while (retval > limit);

  return retval;
}

void cleanup(void) {
#ifdef MONITORSTATS
  StatsDestroy();
#endif
  //free(tmp_page);
}


void
fatal_error_signal (int sig)
{
  /*
   * Since this handler is established for more than one kind of signal,
   * it might still get invoked recursively by delivery of some other kind
   * of signal.  Use a static variable to keep track of that.
   */
  if (fatal_error_in_progress)
    raise (sig);
  fatal_error_in_progress = 1;

  cleanup();

  /*
   * Now reraise the signal.  We reactivate the signal's
   * default handling, which is to terminate the process.
   * We could just call exit or abort,
   * but reraising the signal sets the return status
   * from the process correctly.
   */
  signal (sig, SIG_DFL);
  raise (sig);
}

int main (int argc, char * argv[]) {
  /* install signal handler */
  signal (SIGINT, fatal_error_signal);

  /* test parameters */
  int num_iters = 1000;
  len = 4096;

  /* Parse cmd line arguments */
  int iter_supplied = 0;
  int arg_num;

  if (argc < 2) {
    log_err("%s: first argument should be hostname of externRAM system", __func__);
    return 0;
  }

  config = malloc(strlen(argv[1])+1);
  strcpy(config,argv[1]);

#ifdef TIMING
  cpu_freq_mhz = cpufreq();
  log_info("%s: cpu frequency is %f Mhz", __func__, cpu_freq_mhz);
  TimingStatsInitGlobals();
   max_bucket_slots = 8192;
#endif

  for (arg_num = 1; arg_num < argc; arg_num++) {
    if (strcmp(argv[arg_num], "-i") == 0) {
      if (argc > (arg_num + 1)) {
        num_iters = atoi(argv[arg_num + 1]);
        arg_num++;
      }
      else {
        printf("    With -i flag must supply number of iterations\n\n");
        return 1;
      }
    }
    else if (strcmp(argv[arg_num], "-s") == 0) {
      if (argc > (arg_num + 1)) {
        len = atoi(argv[arg_num + 1]);
        arg_num++;
      }
      else {
        printf("    With -s flag must supply page size\n\n");
        return 1;
      }
    }
  }

  /*
   * change the oom_score to 0 so we are least likely to get killed
   * when using lots of memory
   */
  if(change_oom_score(0) != 0) {
    return 1;
  }

#ifdef MONITORSTATS
#ifdef TIMING
  TimingStatsInit();
#endif
  MonitorStatsInit();
#endif


  /* Choose a random number for FD */
  int random_fd = rand_lim(2<<15 - 1);

  /* Set up connection to RAMCloud and create table */
  uint8_t upid[8];
  pid_t my_pid = getpid();

  /* create a upid */
  srand (time(NULL));
  uint16_t upid_counter = 0;
  uint64_t upid64=0;

  *((uint16_t*) &upid[0]) = get_node_id(); // node id hashed over 16 bits
  *((uint32_t*) &upid[2]) = (uint32_t) my_pid; // pid value on the system

  upid_counter = rand() % (2<<16); // random value up to 65k
  *((uint16_t*) &upid[6]) = upid_counter;
  log_debug("%s: node_id=%hx pid=%u unique_counter=%hu",
            __func__, *((uint16_t*) &upid[0]), *((uint32_t*) &upid[2]), *((uint16_t*) &upid[6]));
  memcpy(&upid64, upid, sizeof(uint8_t) * 8);
  log_debug("%s: upid64=0x%llx", __func__, upid64);

  add_upid_in_map(random_fd, upid64);
  register_with_externram(config, random_fd);
  struct externRAMClient *client = get_client_by_fd(random_fd);

  /* Choose starting key */
  uint64_t rand_index = (LONG_MAX) - num_iters;

  uint8_t value;

  /* write the first byte to the array */
  uint8_t c = 1;

  /* initialize a single page */
  tmp_page = get_local_tmp_page();

  int iter = 0;
  while(iter < num_iters) {
    rand_index += 1;

    *((uint8_t *) &((uint8_t*) tmp_page)[0]) = c;

    if (client) {
      declare_timers();
      start_timing_bucket(start, WRITE_PAGE);
      writePage(client, rand_index, tmp_page);
      stop_timing(start, end, WRITE_PAGE);
#ifdef TIMING
      start = 0, end = 0;
#endif
      start_timing_bucket(start, READ_PAGE);
      readPage(client, rand_index, tmp_page);
      stop_timing(start, end, READ_PAGE);
    }
    else
      log_err("%s: failed writing page %p", __func__, rand_index);

    /* change the character */
    c = rand_lim(255);

    iter++;
  }

  int i, batch_size = 10;
  void ** bufs = malloc(batch_size * sizeof(void *));
  uint64_t * keys = malloc(batch_size * sizeof(uint64_t));
  int * lengths = malloc(batch_size * sizeof(int));

  int numWrite = 0;

  /* Choose starting key */
  rand_index = (LONG_MAX) - num_iters;

  for(i=0;i<batch_size;i++) {
    bufs[numWrite] = malloc(PAGE_SIZE);
    *((uint8_t *) &((uint8_t*) bufs[numWrite])[0]) = c;
    keys[numWrite] = rand_index;
    lengths[numWrite] = PAGE_SIZE;
    numWrite++;
    rand_index += 1;
    /* change the character */
    c = rand_lim(255);
  }

  iter = 0;
  while(iter < num_iters) {
    if (client) {
      declare_timers();
      start_timing_bucket(start, WRITE_PAGES);
      writePages(client, keys, numWrite, bufs, lengths);
      stop_timing(start, end, WRITE_PAGES);
#ifdef TIMING
      start = 0, end = 0;
#endif
      start_timing_bucket(start, READ_PAGES);
      readPages(client, keys, numWrite, bufs, lengths);
      stop_timing(start, end, READ_PAGES);
    }
    else
      log_err("%s: failed writing pages", __func__);


    iter++;
  }

#ifdef TIMING
  StatsPrintBuckets(stdout);
  //StatsPrintSingleBucket(WRITE_PAGES,stdout, 0, num_iters);
#endif

  cleanup();
  return 0;
}
