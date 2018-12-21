/*
 * Copyright 2017 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, April 2017
 */

#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/user.h>
#include <unistd.h>

#include <dbg.h>
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
  if (arr && ufd) {
    close(ufd);
  }
  if (shadow)
    free(shadow);
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
  int num_iters = 100;
  len = 1024 * 500;
  bool dirty = false;

#ifdef TIMING
  cpu_freq_mhz = cpufreq();
  log_info("%s: cpu frequency is %f Mhz", __func__, cpu_freq_mhz);
  TimingStatsInitGlobals();
  max_bucket_slots = 8192;
#endif

  /* Parse cmd line arguments */
  int iter_supplied = 0;
  int arg_num;
  for (arg_num = 0; arg_num < argc; arg_num++) {
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
    else if (strcmp(argv[arg_num], "-l") == 0) {
      if (argc > (arg_num + 1)) {
        len = atoi(argv[arg_num + 1]);
        arg_num++;
      }
      else {
        printf("    With -l flag must supply length of userfault array\n\n");
        return 1;
      }
    }
    else if (strcmp(argv[arg_num], "-d") == 0) {
      dirty = true;
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

/* allocate the userfault region */
  //arr = (uint8_t*)allocate_userfault(&ufd, len * sizeof(uint8_t));
  arr = (uint8_t*)malloc(len * sizeof(uint8_t));
  if (!arr) {
    log_err("failed to allocate userfault region");
    return 1;
  }

  /* shadow array should mirror arr, but non-userfault region */
  shadow = (uint8_t*)malloc(len * sizeof(uint8_t));
  if (!shadow) {
    log_err("failed to allocate shadow region");
    return 1;
  }

  if (dirty) {
    /* dirty the data */
    printf("now dirtying pages\n");
    memset(arr,0,sizeof(uint8_t)*len);
    memcpy(shadow,arr,sizeof(uint8_t)*len);
  }

  uint8_t value;
  int rand_index;

  /* write the first byte to the array */
  uint8_t c = 1;

  int iter = 0;
  while(iter < num_iters) {
    /*
     * Print a random page aligned value from the array.
     * On first access tests the case of reading the zero-page (unitialized)
     * on subsequent accesses, it should either have the value in the page cache
     * or externram.
     */
    rand_index = rand_lim(len-1)/1024 * 1024;

    declare_timers();
    start_timing_bucket(start, READ_PAGE);
    value = arr[rand_index];
    stop_timing(start, end, READ_PAGE);

    check(shadow[rand_index] == value, "value = %u, but shadow has %u",
          value, shadow[rand_index]);

    //printf("READ: arr[%d]=%u (%p)\n", rand_index, value, &arr[rand_index]);

    /* write the new character to a byte in it's own page */
    //printf("WRITE: arr[%d]=%u (%p)\n", rand_index, c, &arr[rand_index]);

#ifdef TIMING
    start=0;
    end=0;
#endif
    start_timing_bucket(start, WRITE_PAGE);
    arr[rand_index] = c;
    stop_timing(start, end, WRITE_PAGE);
    shadow[rand_index] = arr[rand_index];

    /*
     * verify that the expected value was read from arr[] and
     * placed in shadow[]
     */
    check(shadow[rand_index] == c,
          "value read from arr is %u, but %u was written to shadow",
          c, shadow[rand_index]);

    /* change the character */
    c = rand_lim(255);

    iter++;
  }

#ifdef TIMING
  StatsPrintBuckets(stdout);
//  StatsPrintSingleBucket(WRITE_PAGE,stdout, 0, num_iters);
#endif

  cleanup();
  return 0;

error:
  return 1;
}
