/* Copyright 2016 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, December 2016
 */

/****
 *

  The monitor process is always running on a system that has application
  using the userfault feature. A few threads have different purposes

  1) Main thread - listens on a UNIX domain socket, socket_fd for incoming
     file descriptors from client applications.
  2) New file descriptor event hanlder threads
     - A new one for each new FD received on socket_fd
     - These are short-lived
  3) Remove file descriptor event hanlder thread
     - Called by reaper thread or on read failure in handle_userfault
     - These are short-lived
  4) Polling thread - this does the real work. Polls a list of userfaultfd's
     and handles the action (page fault--typically fill with zeroes)
  5) Reaper thread - run in background to look for dead PIDs that have
     been registered with monitor
 *
  epoll example from: https://banu.com/blog/2/how-to-use-epoll-a-complete-example-in-c/
 ****/

/* FluidMem includes */
#include <affinity.h>  /* should be the first to be included */
#include <userfault.h>
#include <dbg.h>
#include <cpufreq.h>
#include "ui_processing.h"
#include "pollfd_vector.h"
#include <threaded_io.h>
#include <buffer_allocator_array.h>

/* cstdlib includes */
#include <stdbool.h>
#include <string.h> /* for memcpy */
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/user.h> /* for PAGE_MASK */
#include <assert.h>

/* for unix domain socket */
#include <sys/socket.h>
#include <linux/un.h>

#include <poll.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <netdb.h>

#include <malloc.h>
#include <sys/mman.h>
#include <pthread.h>
#include <getopt.h>

#define MAX_THREADS 100
#define MAXEVENTS 100
#define MAX_UI_PROCESSING_THREADS 5

#define MAX_ZK_STRING_LEN 22
// maximum of "111.111.111.111:65385"

#ifdef PAGECACHE
int is_test_readahead = 0;
extern int page_cache_size;
extern int prefetch_size;
extern int enable_prefetch;
#endif

#include <monitorstats.h>
MonitorStats* _pstats;

#ifdef TIMING
#include <timingstats.h>
uint32_t max_bucket_slots;
char ** reverse_buckets;
uint32_t buckets_mask;
TimingBucket * timing_buckets;
#endif

extern int cache_size;

volatile sig_atomic_t fatal_error_in_progress = 0;

Pollfd_vector pollfd_vector;
extern char * config;

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
volatile sig_atomic_t toEnd=0;
int stop_by_ui = 0;
int num_threads = -1;
int num_ui_threads = 0;
pthread_t workers[MAX_THREADS];
pthread_t ui_worker;
pthread_t main_worker;
pthread_t polling_worker;
#ifdef REAPERTHREAD
pthread_t reaper_worker;
#endif

int ufd;  // the temporary recveived file descriptor
int socket_fd;
char socket_path[] = "/var/run/fluidmem/monitor.socket";
/* set up epoll listener */
int efd, s;
struct epoll_event event;
struct epoll_event *events;

#ifdef THREADED_REINIT
#define TMP_READ_PAGE_BUFFER_SIZE 100
#define TMP_READ_PAGE_INIT_BATCH_SIZE 50
#define TMP_READ_PAGE_NAME "temporary read page"
#define TMP_EVICT_PAGE_BUFFER_SIZE 100
#define TMP_EVICT_PAGE_INIT_BATCH_SIZE 50
#define TMP_EVICT_PAGE_NAME "temporary evict page"
page_buffer_info* buf_readpage = NULL;
page_buffer_info* buf_evictpage = NULL;
#endif

/* Initialize the poll_list */
int init_poll_list(void) {
  log_trace_in("%s", __func__);

  if (pollfd_vector_init(&pollfd_vector) < 0) {
    log_err("%s: pollfd_vector_init", __func__);
    return -1;
  }

  pollfd_vector.reload_addfd = eventfd(0, EFD_CLOEXEC);
  if (pollfd_vector.reload_addfd == -1) {
      log_err("%s: Opening reload_addfd", __func__);
      return -1;
  }

  pollfd_vector.reload_removefd = eventfd(0, EFD_CLOEXEC);
  if (pollfd_vector.reload_removefd == -1) {
      log_err("%s: Opening reload_removefd", __func__);
      return -1;
  }

  // First pollfd in array is reload_addfd
  struct pollfd reload_add;
  reload_add.fd = pollfd_vector.reload_addfd;
  reload_add.events = POLLIN;
  reload_add.revents = 0;
  pollfd_vector_append(&pollfd_vector, reload_add);

  // Second pollfd in array is reload_addfd
  struct pollfd reload_remove;
  reload_remove.fd = pollfd_vector.reload_removefd;
  reload_remove.events = POLLIN;
  reload_remove.revents = 0;
  pollfd_vector_append(&pollfd_vector, reload_remove);

  log_trace_out("%s", __func__);
  return 0;
}


/*
 * remove_ufd_handler(void *userfault_fd)
 *
 * Remove file descriptor from pollfd_vector,
 * notify the polling thread, and exit
 */
void *remove_ufd_handler(void *userfault_fd) {
  log_trace_in("%s", __func__);
  setThreadCPUAffinity(CPU_FOR_REMOVE_UFD_HANDLER_THREAD, "remove_ufd_handler", TID());

  int ufd = *(int*)userfault_fd;
  log_info("%s: started remove_ufd_handler for ufd %d", __func__, ufd);
  if (flush_buffers(ufd) != 0) {
    log_debug("%s: flush_buffers failed", __func__);
  }

  num_threads--;
  free(userfault_fd);
  log_trace_out("%s", __func__);
  pthread_exit(NULL);
}


/*
 * handle_userfault(int fd)
 *
 * Called by the polling thread when it had an revent on ufd. Perform the read and
 * take the appropriate action using the libuserfault API
 */
int handle_userfault(int ufd) {
  log_trace_in("%s", __func__);
  int ret = -1;
  int rc = -1;
  int type;
  struct uffd_msg msg;
  uint64_t pageaddr;
  declare_timers();

  /* Read from the ufd to get the address of the userfault */
  ret = read(ufd, &msg, sizeof(msg));
  if (ret < 0) {
      if (errno == EAGAIN) {
        // EAGAIN is okay because the uffd is set with O_NONBLOCK
        discard_timing();
        goto handle_userfault_out;
      }
      log_err("%s: Read of uffd (%d) returned %d", __func__, ufd, ret);
      discard_timing();
      return ret;
  }
  else if (ret < sizeof(struct uffd_msg)) {
      log_err("%s: Read was short on uffd (%d), gave %d bytes", __func__, ufd, ret);
      discard_timing();
      return ret;
  }
  else if (ret > sizeof(struct uffd_msg)) {
      log_err("%s: Read on uffd (%d) returned more than one fault, %d bytes", __func__, ufd, ret);
      discard_timing();
      return ret;
  }

  log_debug("%s: Read of uffd (%d) returns %d bytes", __func__, ufd, ret);

  switch (msg.event) {
    case UFFD_EVENT_PAGEFAULT:
      /* read was succesful. now deal with fault at pageaddr */
      pageaddr = (uint64_t)msg.arg.pagefault.address;

      /* Now get rid of flags encoded in address */
      pageaddr &= (uint64_t)(PAGE_MASK);

      start_timing_bucket(start, READ_FROM_EXTERNRAM);
      ret = read_from_externram(ufd, (void*)(uintptr_t)pageaddr);
      stop_timing(start, end, READ_FROM_EXTERNRAM);

      if (ret < 0) {
        log_err("%s: read_from_externram", __func__);
        discard_timing();
        return ret;
      }
      else if (ret > 0) {
        // there was a problem reading from this ufd, so remove from poll list
        // ufd is contained in ret
        log_debug("%s: removing fd %d from pollfd_vector", __func__, ret);
        if (del_fd(&pollfd_vector, ret) == 0) {
          int * fd = malloc(sizeof(*fd));
          *fd = ret;

          num_threads++;
          rc = pthread_create(&workers[num_threads], NULL, remove_ufd_handler, fd);
          if (rc) {
            num_threads--;
            log_err("return code from remove_ufd_handler() is %d", rc);
            discard_timing();
            return rc;
          }
        }
      }

      // don't record timings on EINVAL
      if ((ret > 0) && (errno == EINVAL)) {
        discard_timing();
      }
      break;
    case UFFD_EVENT_REMOVE:
    case UFFD_EVENT_UNMAP:
    case UFFD_EVENT_FORK:
    case UFFD_EVENT_REMAP:
      log_warn("%s: Read event %u from uffd (%d)", __func__, msg.event, ufd);

      log_debug("%s: removing fd %d from pollfd_vector", __func__, ufd);
      if (del_fd(&pollfd_vector, ufd) == 0) {
        int * fd = malloc(sizeof(*fd));
        *fd = ufd;

        num_threads++;
        rc = pthread_create(&workers[num_threads], NULL, remove_ufd_handler, fd);
        if (rc) {
          num_threads--;
          log_err("return code from remove_ufd_handler() is %d", rc);
          return rc;
        }
      }

      break;
    default:
      log_err("%s: Read unexpected event %u from uffd (%d)", __func__, msg.event, ufd);
      discard_timing();
      return -1; /* It's not a page fault, shouldn't happen */
  }

handle_userfault_out:
  log_trace_out("%s", __func__);
  return 0;
}


/*
 * poll_on_ufds(void)
 *
 * Called by the polling thread. It needs to be thread safe, since
 * new_ufd_handler could try to add a file descriptor to pollfd_vector
 * at any time, which will be signalled by an event on reload_addfd or
 * remove a file descriptor signalled by an event on reload_removefd
 */
void poll_on_ufds(void) {
  log_trace_in("%s", __func__);

  int i;
  int rc;
  int num_ufds;
  int ufd = 0;
  uint64_t ufd64 = 0;
  declare_timers();

  while (true) {
    num_ufds = pollfd_vector.size;

    log_trace("%s: enter poll loop", __func__);
    if (poll(pollfd_vector.list, num_ufds, -1 /* Wait forever */) == -1) {
      log_err("%s: poll on pollfd_vector with %d fds", __func__, num_ufds);
      break;
    }

    if (fatal_error_in_progress) {
      break;
    }

    if (pollfd_vector.list[0].revents > 0) {
      /* There's an event on reload_addfd */

      if (pollfd_vector.list[0].revents & POLLERR) {
        log_err("%s: poll on reload_fd retured POLLERR", __func__);
        break;
      }

      if (!(pollfd_vector.list[0].revents & POLLIN)) {
        log_err("%s: poll on reload_fd didn't return POLLIN, revents=%lx", __func__, pollfd_vector.list[0].revents);
        break;
      }

      log_trace("%s: event on reload_fd", __func__);
      if (read(pollfd_vector.list[0].fd, &ufd64, sizeof(uint64_t)) != sizeof(uint64_t)) {
        log_err("%s: invalid read of reload_fd", __func__);
      }

      log_lock("%s: unlocking add_fd_lock",  __func__);
      pthread_mutex_unlock(&pollfd_vector.add_fd_lock);
      log_lock("%s: unlocked add_fd_lock",  __func__);

      ufd = (int) ufd64;
      if (ufd <= 0) {
        log_err("%s: read invalid fd %d from reload_ufd", __func__, ufd);
        break;
      }
      log_debug("%s: read fd %d from reload_ufd", __func__, ufd);

      add_fd(&pollfd_vector, ufd);
    }

    if (pollfd_vector.list[1].revents > 0) {
      /* There's an event on reload_removefd */

      if (pollfd_vector.list[1].revents & POLLERR) {
        log_err("%s: poll on remove_fd retured POLLERR", __func__);
        break;
      }

      if (!(pollfd_vector.list[1].revents & POLLIN)) {
        log_err("%s: poll on remove_fd didn't return POLLIN, revents=%lx", __func__, pollfd_vector.list[1].revents);
        break;
      }

      log_trace("%s: event on reload_removefd", __func__);
      if (read(pollfd_vector.list[1].fd, &ufd64, sizeof(uint64_t)) != sizeof(uint64_t)) {
        log_err("%s: invalid read of reload_removefd", __func__);
        break;
      }

      log_lock("%s: unlocking remove_fd_lock",  __func__);
      pthread_mutex_unlock(&pollfd_vector.remove_fd_lock);
      log_lock("%s: unlocked remove_fd_lock",  __func__);

      ufd = (int) ufd64;
      if (ufd <= 0) {
        log_err("%s: read invalid fd %d from reload_removefd", __func__, ufd);
        break;
      }
      log_debug("%s: read fd %d from reload_removefd", __func__, ufd);

      log_debug("%s: removing fd %d from pollfd_vector", __func__, ufd);
      if (del_fd(&pollfd_vector, ufd) == 0) {
        int * fd = malloc(sizeof(*fd));
        *fd = ufd;

        num_threads++;
        rc = pthread_create(&workers[num_threads], NULL, remove_ufd_handler, fd);
        if (rc) {
          num_threads--;
          log_err("return code from remove_ufd_handler() is %d", rc);
          return;
        }
      }

    }

    /* pollfd_vector.list[0] is reload_addfd */
    /* pollfd_vector.list[1] is reload_removefd */
    for (i = 2; i < pollfd_vector.size; i++) {
      /* There's an event on one of the usefaultfd's */
      if (pollfd_vector.list[i].revents > 0) {
        if (pollfd_vector.list[i].revents & POLLERR) {
          log_err("%s: poll on pollfd_vector index %d retured POLLERR", __func__, i);
          break;
        }
        if (!(pollfd_vector.list[i].revents & POLLIN)) {
          log_err("%s: poll on pollfd_vector index %d didn't return POLLIN, revents=%lx", __func__, i, pollfd_vector.list[i].revents);
          break;
        }

        log_debug("%s: handling event on pollfd_vector index %d with size %d",
                  __func__, i, pollfd_vector.size);
#ifdef TIMING
        discard = 0;
        bucket_index = 0;
#endif

        start_timing_bucket(start, bucket_index);
        rc = handle_userfault(pollfd_vector.list[i].fd);
        stop_timing_discard(start, end, discard, bucket_index);
#ifdef TIMING
        store_bucket(HANDLE_USERFAULT_ALL, (float)((double)(end - start)/(double)(cpu_freq_mhz)));
#endif

        if (rc < 0) {
          log_err("%s: handle_userfault failed on ufd %d: ", __func__, pollfd_vector.list[i].fd);
          break;
        }
      }
    }
  }

  log_info("%s: shutting down poll loop", __func__);
  pollfd_vector_close(&pollfd_vector);
  pollfd_vector_free(&pollfd_vector);

  log_trace_out("%s", __func__);
}


/*
 * *polling_thread(void * tmp)
 *
 * Called at start of program and remains running forever.
 */
void *polling_thread(void * tmp) {
  log_trace_in("%s", __func__);
  setThreadCPUAffinity(CPU_FOR_POLLING_THREAD, "polling_thread", TID());

  poll_on_ufds();

  log_trace_out("%s", __func__);
  pthread_exit(NULL);
}

#ifdef REAPERTHREAD
/*
 * *reaper_thread(void * tmp)
 *
 * Called at start of program and remains running forever.
 */
void *reaper_thread(void * tmp) {
  log_trace_in("%s", __func__);
  setThreadCPUAffinity(CPU_FOR_REAPER_THREAD, "reaper_thread", TID());

  int rc = 0, i = 0, num_dead_fds = 0;

  while (true) {
    int * ufd_list = malloc(1 * sizeof(int));
    int ** ufd_list_ptr = &ufd_list;

    num_dead_fds = purgeDeadUpids(ufd_list_ptr);
    for (i = 0; i < num_dead_fds; i++) {
      int * fd = malloc(sizeof(*fd));
      *fd = (*ufd_list_ptr)[i];

      log_debug("%s: removing fd %d from pollfd_vector", __func__, *fd);
      if (del_fd(&pollfd_vector, *fd) == 0) {
        num_threads++;
        rc = pthread_create(&workers[num_threads], NULL, remove_ufd_handler, fd);
        if (rc) {
          num_threads--;
          log_err("return code from remove_ufd_handler() is %d", rc);
          break;
        }
      }
    }

    free(ufd_list);
    sleep(10);
  }

  log_trace_out("%s", __func__);
  pthread_exit(NULL);
}
#endif

/*
 * new_ufd_handler(void *userfault_fd)
 *
 * New FD event thread. Purpose is to add the file descriptor to pollfd_vector,
 * notify the polling thread, and exit
 */
void *new_ufd_handler(void *userfault_fd) {
  log_trace_in("%s", __func__);
  setThreadCPUAffinity(CPU_FOR_NEW_UFD_HANDLER_THREAD, "new_ufd_handler", TID());

  int ufd = *(int*)userfault_fd;
  uint64_t ufd64 = (uint64_t) ufd;
  log_info("%s: started new_ufd_handler for ufd %d", __func__, ufd);

  log_lock("%s: locking add_fd_lock",  __func__);
  pthread_mutex_lock(&pollfd_vector.add_fd_lock);
  log_lock("%s: locked add_fd_lock",  __func__);

  if (write(pollfd_vector.reload_addfd, &ufd64, sizeof(uint64_t)) != sizeof(uint64_t)) {

    log_lock("%s: unlocking add_fd_lock",  __func__);
    pthread_mutex_unlock(&pollfd_vector.add_fd_lock);
    log_lock("%s: unlocked add_fd_lock",  __func__);

    log_err("%s: failed to write ufd %llu to reload_addfd", __func__, ufd64);
    goto exit;
  }

exit:
  num_threads--;
  free(userfault_fd);
  log_trace_out("%s", __func__);
  pthread_exit(NULL);
}

void
fatal_error_signal_do_nothing (int sig)
{
}

/*
 * fatal_error_signal (int sig)
 *
 * Called on interrupt to cleanup any state (memory allocations, userfaultfd's)
 */
void
fatal_error_signal (int sig)
{
  log_trace_in("%s", __func__);

  /* Since this handler is established for more than one kind of signal,
     it might still get invoked recursively by delivery of some other kind
     of signal.  Use a static variable to keep track of that. */
  if (fatal_error_in_progress)
    raise (sig);
  fatal_error_in_progress = 1;

  signal (SIGINT, fatal_error_signal_do_nothing); // To prevent multiple execution of this function

#ifdef MONITORSTATS
  StatsDestroy();
#endif

#ifdef PAGECACHE
  pageCacheCleanup();
#endif

  int j=0;
  for( j=0; j<num_threads; j++ )
  {
    pthread_cancel(workers[j]);
  }
  if( stop_by_ui!=1 )
  {
    pthread_cancel(ui_worker);
  }
#ifdef REAPERTHREAD
  pthread_cancel(reaper_worker);
#endif
  pthread_cancel(main_worker);

#ifdef ENABLE_AFFINITY
  destroyTopology();
#endif

  if(zookeeperConn)
    free(zookeeperConn);

  toEnd=1;
  log_trace_out("%s", __func__);
}

void *main_thread(void * tmp) {
  log_trace_in("%s", __func__);
  setThreadCPUAffinity(CPU_FOR_MAIN_THREAD, "main_thread", TID());

  int rc = 0;
  event.data.fd = socket_fd;
  event.events = EPOLLIN | EPOLLET;

  efd = epoll_create1 (0);
  if (efd == -1)
  {
    log_err("%s: epoll_create", __func__);
    return (void*)-1;
  }

  /* insert epoll handler */
  if(epoll_ctl(efd, EPOLL_CTL_ADD, socket_fd, &event) < 0) {
    log_err("%s: Failed to insert handler to epoll", __func__);
    return (void*)-1;
  }

  events = calloc (MAXEVENTS, sizeof event);

  while (1) {
    int n, i;

    n = epoll_wait (efd, events, MAXEVENTS, -1);
    if (n == -1) {
      log_err("%s: epoll_wait", __func__);
      return (void*)-1;
    }
    for (i = 0; i < n; i++) {
      if (socket_fd == events[i].data.fd) {
        /* We have a notification on the listening socket, which
           means one or more incoming connections. */
        while (1) {
          struct sockaddr in_addr;
          socklen_t in_len;
          int infd;

          in_len = sizeof in_addr;
          infd = accept (socket_fd, &in_addr, &in_len);
          if (infd == -1) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
              /* We have processed all incoming connections */
              break;
            }
            else {
              log_err("%s: accept", __func__);
              break;
            }
          }

          /* Make the incoming socket non-blocking and add it to the
             list of fds to monitor. */
          s = setnonblocking (infd);
          if (s == -1)
            return (void*)1;

          event.data.fd = infd;
          event.events = EPOLLIN | EPOLLET;
          s = epoll_ctl (efd, EPOLL_CTL_ADD, infd, &event);
          if (s == -1) {
            log_err("%s: epoll_ctl", __func__);
            return (void*)-1;
          }
        }
        continue;
      }
      else {
        ufd = recv_fd(events[i].data.fd);
        if (ufd < 0)
          log_err("%s: recv_fd", __func__);

        int * fd = malloc(sizeof(*fd));
        *fd = ufd;

        num_threads++;
        rc = pthread_create(&workers[num_threads], NULL, new_ufd_handler, fd);
        if (rc) {
          num_threads--;
          log_err("%s: return code from new_ufd_handler() is %d", __func__, rc);
        }

        close (events[i].data.fd);
      }
    }
  }

  log_trace_out("%s", __func__);
}

int main (int argc, char *argv[])
{
  log_trace_in("%s", __func__);

  signal (SIGINT, fatal_error_signal);

  // initialize globals
  return_val = 0;
  print_info = 0;
  exit_on_recoverable_error = 0;

#ifdef ENABLE_AFFINITY
  initTopology();
#endif
#ifdef TIMING
  TimingStatsInitGlobals();
#endif

  int rc = 0;

  if (argc < 2) {
    log_err("%s: first argument should be hostname of externRAM system", __func__);
    return 0;
  }

  zookeeperConn = malloc(sizeof(char) * MAX_ZK_STRING_LEN);
  strcpy(zookeeperConn,"10.0.1.1:2181");

  char optionStr0[] = "--cache_size=";
#ifdef PAGECACHE
  char optionStr1[] = "--page_cache_size=";
  char optionStr2[] = "--prefetch_size=";
  char optionStr3[] = "--enable_prefetch=";
  char optionStr4[] = "--test_readahead";
#endif
  char optionStr5[] = "--zookeeper=";
  char optionStr6[] = "--print_info";
  char optionStr7[] = "--exit-on-recoverable-error";
#ifdef TIMING
  char optionStr8[] = "--buckets_mask=";
  char optionStr9[] = "--max_bucket_slots=";
#endif

  int i=2;
  while(i < argc)
  {
    if (strncmp(argv[i], optionStr0, sizeof(optionStr0) - 1) == 0) {
      cache_size = atoi(argv[i] + sizeof(optionStr0) - 1);
    }
#ifdef PAGECACHE
    else if (strncmp(argv[i], optionStr1, sizeof(optionStr1) - 1 ) == 0) {
      page_cache_size = atoi(argv[i] + sizeof(optionStr1) - 1);
    }
    else if (strncmp(argv[i], optionStr2, sizeof(optionStr2) - 1) == 0) {
      prefetch_size = atoi(argv[i] + sizeof(optionStr2) - 1);
    }
    else if (strncmp(argv[i], optionStr3, sizeof(optionStr3) - 1) == 0) {
      enable_prefetch = atoi(argv[i] + sizeof(optionStr3) - 1);
    }
    else if (strncmp(argv[i], optionStr4, sizeof(optionStr4) - 1) == 0) {
      is_test_readahead = 1;
    }
#endif
    else if (strncmp(argv[i], optionStr5, sizeof(optionStr5) - 1) == 0) {
      strncpy(zookeeperConn, argv[i] + sizeof(optionStr5) - 1, MAX_ZK_STRING_LEN);
    }
    else if (strncmp(argv[i], optionStr6, sizeof(optionStr6) - 1) == 0) {
      print_info = 1;
    }
    else if (strncmp(argv[i], optionStr7, sizeof(optionStr7) - 1) == 0) {
      exit_on_recoverable_error = 1;
    }
#ifdef TIMING
    else if (strncmp(argv[i], optionStr8, sizeof(optionStr8) - 1) == 0) {
      buckets_mask  = atoi(argv[i] + sizeof(optionStr8) - 1);
    }
    else if (strncmp(argv[i], optionStr9, sizeof(optionStr9) - 1) == 0) {
      max_bucket_slots  = atoi(argv[i] + sizeof(optionStr9) - 1);
    }
#endif
    i++;
  }
  log_info("%s: cache_size = %d", __func__, cache_size);
#ifdef PAGECACHE
  if( is_test_readahead==1 )
    log_info("%s: test_readahead is set", __func__);
  log_info("%s: page_cache_size = %d", __func__, page_cache_size);
  log_info("%s: prefetch_size = %d", __func__, prefetch_size);
  log_info("%s: enable_prefetch = %d", __func__, enable_prefetch);
#endif
  if( print_info==1 )
    log_info("%s: print_info is set", __func__);
  if( exit_on_recoverable_error==1 )
    log_info("%s: exit_on_recoverable_error is set", __func__);

#ifdef TIMING
  log_info("%s: buckets_mask = %d", __func__, buckets_mask);
  log_info("%s: max_bucket_slots = %d", __func__, max_bucket_slots);
  timing_label = malloc(sizeof(char) * 11); // for COPY+EVICT
  cpu_freq_mhz = cpufreq();
  log_info("%s: cpu frequency is %f Mhz", __func__, cpu_freq_mhz);
#endif

#ifdef MONITORSTATS
#ifdef TIMING
  TimingStatsInit();
#endif
  MonitorStatsInit();
#endif

  // memset zero page
  zeroPage = malloc(PAGE_SIZE);
  memset(zeroPage, 0, PAGE_SIZE);

  config = malloc(strlen(argv[1])+1);
  strcpy(config,argv[1]);

  create_buffers();

  rc = init_poll_list();
  if (rc < 0) {
    return rc;
  }

  socket_fd = create_server(socket_path);
  if (socket_fd < 0) {
    log_err("%s: create_server", __func__);
    return socket_fd;
  }

  /* start polling before we enter event loop */
  rc = pthread_create(&polling_worker, NULL, polling_thread, (void *)NULL);
  if (rc) {
    log_err("%s: return code from polling_thread() is %d", __func__, rc);
    return rc;
  }

#ifdef THREADED_WRITE_TO_EXTERNRAM
  sem_init(&writer_sem, 0, 0);
#endif
#ifdef THREADED_PREFETCH
  sem_init(&prefetcher_sem, 0, 0);
#endif

#if defined(THREADED_WRITE_TO_EXTERNRAM) || defined(THREADED_PREFETCH)
  rc = pthread_mutex_init(&list_lock, NULL);
  if(rc)
  {
    log_err("%s: write list lock init failed", __func__);
    return rc;
  }
  sem_init(&ufhandler_sem, 0, 0);
#endif

#ifdef THREADED_WRITE_TO_EXTERNRAM
  /* start writing to exterRAM processing thread */
  rc = pthread_create(&write_worker, NULL, write_into_externram_thread, (void *)NULL);
  if (rc) {
    log_err("%s: return code from write_into_externram_thread() is %d", __func__, rc);
    return rc;
  }
#endif
#ifdef THREADED_PREFETCH
  /* start writing to exterRAM processing thread */
  rc = pthread_create(&prefetch_worker, NULL, prefetch_thread, (void *)NULL);
  if (rc) {
    log_err("%s: return code from prefetch_thread() is %d", __func__, rc);
    return rc;
  }
#endif
#ifdef THREADED_REINIT
  buf_readpage = init_page_buffer(TMP_READ_PAGE_BUFFER_SIZE, TMP_READ_PAGE_INIT_BATCH_SIZE, TMP_READ_PAGE_NAME, CPU_FOR_REINIT_READPAGE_THREAD);
  if (buf_readpage==NULL) {
    log_err("failed to preallocate %s buffer", TMP_READ_PAGE_NAME);
    return -1;
  }

  /* start reinitialization thread for read pages */
  rc = pthread_create(&buf_readpage->worker, NULL, reinit_thread, (void *)buf_readpage);
  if (rc) {
    log_err("%s: return code from reinit_thread() is %d", __func__, rc);
    return rc;
  }

  buf_evictpage = init_page_buffer(TMP_EVICT_PAGE_BUFFER_SIZE, TMP_EVICT_PAGE_INIT_BATCH_SIZE, TMP_EVICT_PAGE_NAME, CPU_FOR_REINIT_EVICTPAGE_THREAD);
  if (buf_evictpage==NULL) {
    log_err("failed to preallocate %s buffer", TMP_EVICT_PAGE_NAME);
    return -1;
  }

  /* start reinitialization thread for evict pages */
  rc = pthread_create(&buf_evictpage->worker, NULL, reinit_thread, (void *)buf_evictpage);
  if (rc) {
    log_err("%s: return code from reinit_thread() is %d", __func__, rc);
    return rc;
  }
#endif

#ifdef REAPERTHREAD
  /* start dead process page reaper thread */
  rc = pthread_create(&reaper_worker, NULL, reaper_thread, (void *)NULL);
  if (rc) {
    log_err("%s: return code from reaper_thread() is %d", __func__, rc);
    return rc;
  }
#endif

  /* start user interface processing thread */
  rc = pthread_create(&ui_worker, NULL, ui_processing_thread, (void *)NULL);
  if (rc) {
    log_err("%s: return code from ui_processing_thread() is %d", __func__, rc);
    return rc;
  }

  rc = pthread_create(&main_worker, NULL, main_thread, (void *)NULL);
  if (rc) {
    log_err("%s: return code from main_thread() is %d", __func__, rc);
    return rc;
  }

  while(true)
  {
    sleep(1);
    if(toEnd==1)
      break;
  }
  free (events);

  close(socket_fd);

  clean_up_lock();

  log_info("%s: return_val: %d", __func__, return_val);
  log_trace_out("%s", __func__);
  return return_val;
}

