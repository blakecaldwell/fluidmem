#ifndef __dbg_h__
#define __dbg_h__

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>

#define MAX_DATESTAMP_LEN 20
int print_info;
int exit_on_recoverable_error;
int return_val;

static __inline __attribute__((always_inline))
uint64_t
rdtsc()
{
  uint32_t lo, hi;
  __asm__ __volatile__("rdtsc" : "=a" (lo), "=d" (hi));
  return (((uint64_t)hi << 32) | lo);
}

static __inline __attribute__((always_inline))
int TID() {
  return (int) syscall(__NR_gettid);
}

static __inline __attribute__((always_inline))
void
TIMESTMP() {
  char date[MAX_DATESTAMP_LEN];
  struct timeval tv;
  struct tm       *tm;

  gettimeofday(&tv, NULL);
  if((tm = localtime(&tv.tv_sec)) != NULL) {
    strftime(date, 20, "%Y-%m-%dT%H:%M:%S", tm);
    fprintf(stderr, "[%s.%03d] ", date, tv.tv_usec / 1000);
  }
}

#ifdef TIMING
int discard;
float cpu_freq_mhz;
char * timing_label;
#define start_timing(B) B = rdtsc();
#define end_timing(E) E = rdtsc();
#define declare_timers() uint64_t start = 0, end = 0;
#define start_timing_bucket(S, B) {\
  if (((1 << B) & buckets_mask) != 0) {\
    S = rdtsc();\
  }\
}
#define stop_timing(S, E, B) {\
  if (((1 << B) & buckets_mask) != 0) {\
    E = rdtsc();\
    store_bucket(B, (float)((double)(E - S)/(double)(cpu_freq_mhz)));\
  }\
}
#define stop_timing_discard(S, E, D, B) {\
  if (((1 << B) & buckets_mask) != 0) {\
    E = rdtsc();\
    if (D == 0) {\
      store_bucket(B, (float)((double)(E - S)/(double)(cpu_freq_mhz)));\
    }\
  }\
}
#define log_timing(A, M, ...) {TIMESTMP(); fprintf(stderr, "[%d] [TIME] (%s:%d) " M " took %lu cycles (%.2f us)\n", TID(), __FILE__, __LINE__, ##__VA_ARGS__, A, (float)((double)(A)/(double)(cpu_freq_mhz)));}
#define start_timing(B) B = rdtsc();
#define end_timing(E) E = rdtsc();
#define discard_timing() discard = 1;
#else
#define start_timing(B)
#define end_timing(E)
#define declare_timers()
#define start_timing_bucket(S, B)
#define stop_timing(S, E, B)
#define stop_timing_discard(S, E, D, B)
#define discard_timing()
#endif

#define clean_errno() (errno == 0 ? "None" : strerror(errno))

static __inline __attribute__((always_inline))
void exit_with_errno(int val, int errnum)
{
  /* To return only the error number of the first error */
  if( return_val==0 )
    return_val = errnum;
  raise(SIGINT);
}
#define log_warn(M, ...) {\
  TIMESTMP();\
  fprintf(stderr, "[%d] [WARN] (%s:%d: errno: %s) " M "\n", TID(), __FILE__, __LINE__, clean_errno(), ##__VA_ARGS__);\
}
#define log_err(M, ...) {\
  TIMESTMP();\
  exit_with_errno( fprintf(stderr, "[%d] [ERROR] (%s:%d: errno: %s(%d)) " M "\n", TID(), __FILE__, __LINE__, clean_errno(), errno, ##__VA_ARGS__), errno );\
}
#define log_recoverable_err(M, ...) {\
  if (exit_on_recoverable_error == 1) {\
    TIMESTMP();\
    exit_with_errno( fprintf(stderr, "[%d] [ERROR] (%s:%d: errno: %s(%d)) " M "\n" , TID(), __FILE__, __LINE__, clean_errno(), errno, ##__VA_ARGS__), errno );\
  }\
  else {\
    TIMESTMP();\
    fprintf(stderr, "[%d] [WARN] (%s:%d: errno: %s) " M "\n", TID(), __FILE__, __LINE__, clean_errno(), ##__VA_ARGS__);\
  }\
}

#define check(A, M, ...) if(!(A)) { log_err(M, ##__VA_ARGS__); errno=0; goto error; }

#define sentinel(M, ...)  { log_err(M, ##__VA_ARGS__); errno=0; goto error; }

#define check_mem(A) check((A), "Out of memory.")

#define check_debug(A, M, ...) if(!(A)) { debug(M, ##__VA_ARGS__); errno=0; goto error; }

#ifdef TRACE
#define log_trace_in(M, ...) {TIMESTMP(); fprintf(stderr, "[%d] [TRCE] (%s:%d) Entering " M "\n", TID(), __FILE__, __LINE__, ##__VA_ARGS__);}
#define log_trace_out(M, ...) {TIMESTMP(); fprintf(stderr, "[%d] [TRCE] (%s:%d) Leaving " M "\n", TID(), __FILE__, __LINE__, ##__VA_ARGS__);}
#define log_trace(M, ...) {TIMESTMP(); fprintf(stderr, "[%d] [TRCE] (%s:%d) " M "\n", TID(), __FILE__, __LINE__, ##__VA_ARGS__);}
#define log_trace_call(M, ...) M(##__VA_ARGS__)
#else
#define log_trace_in(M, ...)
#define log_trace_out(M, ...)
#define log_trace(M, ...)
#define log_trace_call(M, ...)
#endif

#if defined(DEBUG) || defined(RAMCLOUD_DEBUG)
#define log_debug(M, ...) {TIMESTMP(); fprintf(stderr, "[%d] [DEBG] (%s:%d) " M "\n", TID(), __FILE__, __LINE__, ##__VA_ARGS__);}
#define log_debug_call(M, ...) M(##__VA_ARGS__)
#else
#define log_debug(M, ...)
#define log_debug_call(M, ...)
#endif

#ifdef LOCK_DEBUG
#define log_lock(M, ...) {TIMESTMP(); fprintf(stderr, "[%d] [LOCK] (%s:%d) " M "\n", TID(), __FILE__, __LINE__, ##__VA_ARGS__);}
#else
#define log_lock(M, ...)
#endif

#define log_info(M, ...) {\
  if (print_info == 1) {\
    TIMESTMP();\
    fprintf(stderr, "[%d] [INFO] (%s:%d) " M "\n", TID(), __FILE__, __LINE__, ##__VA_ARGS__);\
  }\
}

#endif
