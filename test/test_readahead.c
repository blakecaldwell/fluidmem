/* Copyright 2015 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, May 2015
 */

#include <stdbool.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "userfault-client.h"

#include <sys/mman.h>
#include <fcntl.h>
#include <asm/unistd_64.h>
#include <sys/ioctl.h>
#include <asm-generic/mman-common.h>
#include <dbg.h>

int isSequential=0; // sequential or random memory access

volatile sig_atomic_t fatal_error_in_progress = 0;
int very_large_len;
int very_large_ufd;
int * arr_small;
int * arr_medium;
int * arr_large;
int * arr_very_large;

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
    if (arr_very_large && very_large_ufd) {
      disable_ufd_area(very_large_ufd, (void *)arr_very_large,very_large_len*sizeof(int));
      close(very_large_ufd);
    }
}


void
fatal_error_signal (int sig)
{
  /* Since this handler is established for more than one kind of signal,
     it might still get invoked recursively by delivery of some other kind
     of signal.  Use a static variable to keep track of that. */
  if (fatal_error_in_progress)
    raise (sig);
  fatal_error_in_progress = 1;

  cleanup();

  /* Now reraise the signal.  We reactivate the signal's
     default handling, which is to terminate the process.
     We could just call exit or abort,
     but reraising the signal sets the return status
     from the process correctly. */
  signal (sig, SIG_DFL);
  raise (sig);
}

int main (int argc, char* argv[]) {
  if (argc != 3) {
    fprintf(stderr,"usage: test_readahead <virtual memory size in MB> [--sequential|--random] \n");
    return 0;
  }
  else {
    char optionStr1[] = "--sequential";
    char optionStr2[] = "--random";
    if( strncmp(argv[2], optionStr1, sizeof(optionStr1)-1)==0 )
      isSequential = 1;
    very_large_len = 1024 * 1024 * atoi(argv[1]) / sizeof(int); // 2KB
  }

  signal (SIGINT, fatal_error_signal);

  int i = 0;
  int rand_index = 0;
  uint64_t pageIdx;
  char * pagehash;

  // change the oom_score to 0 so it won't get killed
  if(change_oom_score(0) != 0) {
    return 1;
  }


  // allocate this large region and then modify in the loop (a new page each time)
  arr_very_large = (int*)allocate_userfault(&very_large_ufd, very_large_len*sizeof(int));
  memset(arr_very_large,0,very_large_len*sizeof(int));

  if (!arr_very_large) {
    log_err("failed to allocate userfault");
    return 1;
  }

  pagehash = malloc(very_large_len/1024);
  memset(pagehash, 0, very_large_len/1024);

  int iter = 0;
  int j;
  while(iter < very_large_len) {
    // print a random page aligned value from the array.
    // on first access tests the case of reading the zero-page (unitialized)
    // on subsequent accesses, it should either have the value in the page cache or RAMCloud
    if( isSequential==1 )
    {
      rand_index += 4; // for sequential testing
      if( rand_index >= very_large_len )
      rand_index = 0;
    }
    else
      rand_index = rand_lim(very_large_len-1);

    pageIdx = rand_index/1024;

    if( pagehash[pageIdx]==1 )
    {
      if ((arr_very_large[rand_index] >= 32) &&
        (arr_very_large[rand_index] <= 126))
        printf("%c",arr_very_large[rand_index]);
      else
        ;
      for( j=0; j<1024; j++)
      {
        arr_very_large[pageIdx*1024+j] += 1;
        if( arr_very_large[pageIdx*1024+j]>126 )
          arr_very_large[pageIdx*1024+j] = 32;
      }
    }
    else
    {
      pagehash[pageIdx] = 1;
      for( j=0; j<1024; j++)
      {
        arr_very_large[pageIdx*1024+j] = rand_lim(94)+32;
      }
      printf("%c",arr_very_large[rand_index]);
    }
    if( iter%100==99 )
      printf("\n");
    iter++;
  }

  sleep(1); // to prevent errors in monitor due to abrupt cleanup
  cleanup();
  if(pagehash)
    free(pagehash);
  return 0;
}
