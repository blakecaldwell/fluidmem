#ifndef __buffer_allocator_h__
#define __buffer_allocator_h__

#include <dbg.h>
#include <sys/mman.h>

#ifndef THREADED_REINIT

/* old method using mmap */
static inline void * get_local_tmp_page(void) {
  log_trace_in("%s", __func__);

  void * local_tmp_page;

  local_tmp_page = mmap(NULL, getpagesize(),PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,-1, 0);
  if (!local_tmp_page) {
      log_err("%s: mapping local tmp page", __func__);
      return NULL;
  }

  log_trace_out("%s", __func__);
  return local_tmp_page;
}

#else
/* for THREADED_REINIT */

#include <unistd.h>
#include <sys/user.h> /* for PAGE_MASK */
#define BUF_NAME_SIZE 40

typedef void** page_buffers;

struct page_buffer_info {
  page_buffers pages;
  int capacity;
  int init_batch_size;
  int idx_current_page;
  int idx_last_initialized_page;
  int num_initialized_pages;
  int thread_cpu;
  char name[40];

  pthread_t worker;
  pthread_mutex_t prod_lock;
  pthread_mutex_t cons_lock;
  pthread_cond_t prod_cond;
  pthread_cond_t cons_cond;
};
typedef struct page_buffer_info page_buffer_info;

inline void lock_for_buffer_prod(page_buffer_info* buf) {
  log_lock("%s: locking buffer_prod_lock", __func__);
  pthread_mutex_lock(&buf->prod_lock);
  log_lock("%s: locked buffer_prod_lock", __func__);
}

inline void unlock_for_buffer_prod(page_buffer_info* buf) {
  log_lock("%s: unlocking buffer_prod_lock", __func__);
  pthread_mutex_unlock(&buf->prod_lock);
  log_lock("%s: unlocked buffer_prod_lock", __func__);
}

inline void lock_for_buffer_cons(page_buffer_info* buf) {
  log_lock("%s: locking buffer_cons_lock", __func__);
  pthread_mutex_lock(&buf->cons_lock);
  log_lock("%s: locked buffer_cons_lock", __func__);
}

inline void unlock_for_buffer_cons(page_buffer_info* buf) {
  log_lock("%s: unlocking buffer_cons_lock", __func__);
  pthread_mutex_unlock(&buf->cons_lock);
  log_lock("%s: unlocked buffer_cons_lock", __func__);
}

inline void * get_tmp_page(page_buffer_info* buf) {
  void * tmp_page = NULL;
  if (!buf->pages) {
    log_err("%s buffer has not been allocated", buf->name);
    return tmp_page;
  }

  lock_for_buffer_cons(buf);
  while (true) { 
    if (buf->num_initialized_pages<=1) {
      log_info("This should not happen if the initialization is fast enough.");
      pthread_cond_wait(&buf->cons_cond, &buf->cons_lock);
    } else {
      break;
    }
  }
 
  log_debug("getting a(n) %s [%d] %p", buf->name, buf->idx_current_page, buf->pages[buf->idx_current_page]);
  tmp_page = buf->pages[buf->idx_current_page];
  buf->idx_current_page++;
  buf->num_initialized_pages--;

  if (buf->idx_current_page>=buf->capacity)
    buf->idx_current_page = 0;

  unlock_for_buffer_cons(buf);

  if (buf->capacity - buf->num_initialized_pages>=buf->init_batch_size+1) {
    log_debug("Waking up the initialization thread for %s.", buf->name);
    lock_for_buffer_prod(buf);
    pthread_cond_signal(&buf->prod_cond);
    unlock_for_buffer_prod(buf);
  }

  log_trace_out("%s", __func__);
  return tmp_page;
}

page_buffer_info* init_page_buffer(int capacity, int init_batch_size, char * name, int thread_cpu) {
  // No locks needed becuase this is called early on before write thread
  // begins.
  log_trace_in("%s", __func__);
  int i;
  struct page_buffer_info* buf = (page_buffer_info*) malloc(sizeof(struct page_buffer_info));

  buf->capacity = capacity;
  buf->init_batch_size = init_batch_size;
  strcpy(buf->name, name);
  buf->thread_cpu = thread_cpu;

  buf->idx_current_page = 0;
  buf->pages = (page_buffers) malloc(buf->capacity * sizeof(void *));
  if (!buf->pages) {
    log_err("%s: mapping %s", __func__, buf->name);
    free(buf);
    return NULL;
  }

  for (i = 0; i < buf->capacity; i++) {
    buf->pages[i] = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    log_debug("initiating a(n) %s [%d] %p", buf->name, i, buf->pages[i]);
    if (buf->pages[i]==(void *) -1) {
      log_err("%s: mapping %s %d", __func__, buf->name, i);
      return NULL;
    }
  }
  buf->num_initialized_pages = buf->capacity;
  buf->idx_last_initialized_page = 0;

  pthread_mutex_init(&buf->prod_lock, NULL);
  pthread_mutex_init(&buf->cons_lock, NULL);
  pthread_cond_init(&buf->prod_cond, NULL);
  pthread_cond_init(&buf->cons_cond, NULL);

  log_trace_out("%s", __func__);
  return buf;
}

int reinit_page_buffer(page_buffer_info* buf) {
  log_trace_in("%s", __func__);
  int i;
  int num_to_init = 0;
  int idx = 0;

  if (!buf->pages) {
    log_err("%s: invalid %s buffer", __func__, buf->name);
    return -1;
  }

  lock_for_buffer_prod(buf);
  while (true) {
    num_to_init = buf->capacity - buf->num_initialized_pages;
    num_to_init--; /* do not initialize the page at idx_current_page */
    if (num_to_init<buf->init_batch_size) {
      pthread_cond_wait(&buf->prod_cond, &buf->prod_lock);
    } else {
      break;
    }
  }
  idx = buf->idx_last_initialized_page;
  for (i = 0; i < num_to_init; i++) {
    if (idx==buf->capacity)
      idx = 0;
    buf->pages[idx] = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    log_debug("reinitiating a(n) %s [%d] %p", buf->name, idx, buf->pages[idx]);
    if (buf->pages[idx]==(void *) -1) {
      log_err("%s: mapping %s %d", __func__, buf->name, idx);
      return -1;
    }
    idx++;
  }

  buf->idx_last_initialized_page += num_to_init;
  if (buf->idx_last_initialized_page>=buf->capacity)
    buf->idx_last_initialized_page -= buf->capacity;
  buf->num_initialized_pages += num_to_init;

  unlock_for_buffer_prod(buf);

  lock_for_buffer_cons(buf);
  pthread_cond_signal(&buf->cons_cond);
  unlock_for_buffer_cons(buf);

  log_trace_out("%s", __func__);
  return 0;
}

void cleanup_page_buffer(page_buffer_info* buf) {
  log_trace_in("%s", __func__);
  int i = 0;
  int idx = 0;
  int num_to_unmap = buf->num_initialized_pages;

  idx = buf->idx_current_page;
  for ( ; i<num_to_unmap; i++ ) {
    munmap(buf->pages[idx],PAGE_SIZE);
    idx++;
    if (idx>=buf->capacity)
      idx = 0;
  }
  free(buf->pages);

  pthread_mutex_destroy(&buf->prod_lock);
  pthread_mutex_destroy(&buf->cons_lock);
  pthread_cond_destroy(&buf->prod_cond);
  pthread_cond_destroy(&buf->cons_cond);

  log_trace_out("%s", __func__);
}

int return_free_page(page_buffer_info* buf, void* addr) {
  log_trace_in("%s", __func__);
  int ret = 0;
  ret = munmap(addr, PAGE_SIZE);
  log_trace_out("%s", __func__);
  return ret;
}

void *reinit_thread(void * tmp) {
  log_trace_in("%s", __func__);
  char thread_name[100];
  page_buffer_info* buf = (page_buffer_info*) tmp;

  sprintf(thread_name, "%s for %s buffer", __func__, buf->name);
  setThreadCPUAffinity(buf->thread_cpu, thread_name, TID());

  while(true)
  {
    reinit_page_buffer(buf);
  }
  log_trace_out("%s", __func__);
}


#endif /* THREADED_REINIT */

#endif
