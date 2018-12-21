#ifndef __threaded_io_h__
#define __threaded_io_h__

#include <semaphore.h>
#define WRITE_BATCH_SIZE 100

typedef struct {
  int ufd;
  char pageaddr[8];
} info_key_t;

typedef struct write_info {
  UT_hash_handle hh2;
  info_key_t key;
  void * page;
} write_info;

typedef struct prefetch_info {
  UT_hash_handle hh3;
  info_key_t key;
} prefetch_info;

pthread_t write_worker;
pthread_t prefetch_worker;
write_info * write_list = NULL;
prefetch_info * prefetch_list = NULL;

pthread_mutex_t list_lock;
// below are protected by list_lock
bool isWriterWaiting = false;
bool isPrefetcherWaiting = false;
bool isUfhandlerWaiting = false;

pthread_mutex_t flush_write_needed_lock;
// below are protected by flush_write_needed_lock
bool flushWriteListNeeded = false;

sem_t writer_sem;
sem_t prefetcher_sem;
sem_t ufhandler_sem;
sem_t flushed_write_sem;

inline void add_write_info( int ufd, uint64_t pageaddr, void * page )
{
  write_info *s;
  s = (write_info *) malloc(sizeof(write_info));
  s->key.ufd = ufd;
  memcpy( s->key.pageaddr, &pageaddr, sizeof(pageaddr) );
  s->page = page;
  HASH_ADD( hh2, write_list, key, sizeof(info_key_t), s );
}

inline bool exist_write_info( int ufd, uint64_t pageaddr )
{
  write_info l, *p = NULL;
  l.key.ufd = ufd;
  memcpy( l.key.pageaddr, &pageaddr, sizeof(pageaddr) );
  HASH_FIND( hh2, write_list, &l.key, sizeof(info_key_t), p );
  if(p==NULL)
    return false;
  else
    return true;
}

inline write_info * get_one_write_info()
{
  write_info *current, *tmp;
  HASH_ITER( hh2, write_list, current, tmp ) {
    return current;
  }
  return NULL;
}

void print_write_info()
{
  write_info *current, *tmp;
  int i=0;
  HASH_ITER( hh2, write_list, current, tmp ) {
    log_debug("%s: %d ufd %d pageaddr %p page %p", __func__, i++, current->key.ufd, current->key.pageaddr, current->page );
  }
}

inline void del_write_info( int ufd, uint64_t pageaddr )
{
  write_info l, *p = NULL;
  l.key.ufd = ufd;
  memcpy( l.key.pageaddr, &pageaddr, sizeof(pageaddr) );
  HASH_FIND( hh2, write_list, &l.key, sizeof(info_key_t), p );
  if(p!=NULL)
  {
    HASH_DELETE( hh2, write_list, p );
    free(p);
  }
  else
    log_err("%s: failed to delete key %llx from write_list", __func__, pageaddr);
}

inline int get_write_list_size()
{
  return HASH_CNT( hh2, write_list );
}

void *write_into_externram_thread(void * tmp);

inline void add_prefetch_info( int ufd, uint64_t pageaddr )
{
  prefetch_info *s;
  s = (prefetch_info *) malloc(sizeof(prefetch_info));
  s->key.ufd = ufd;
  memcpy( s->key.pageaddr, &pageaddr, sizeof(pageaddr) );
  HASH_ADD( hh3, prefetch_list, key, sizeof(info_key_t), s );
}

inline bool exist_prefetch_info( int ufd, uint64_t pageaddr )
{
  prefetch_info l, *p = NULL;
  l.key.ufd = ufd;
  memcpy( l.key.pageaddr, &pageaddr, sizeof(pageaddr) );
  HASH_FIND( hh3, prefetch_list, &l.key, sizeof(info_key_t), p );
  if(p==NULL)
    return false;
  else
    return true;
}

inline prefetch_info * get_one_prefetch_info()
{
  prefetch_info *current, *tmp;
  HASH_ITER( hh3, prefetch_list, current, tmp ) {
    return current;
  }
  return NULL;
}

void print_prefetch_info()
{
  prefetch_info *current, *tmp;
  int i=0;
  HASH_ITER( hh3, prefetch_list, current, tmp ) {
    log_debug("%s: %d ufd %d pageaddr %p", __func__, i++, current->key.ufd, current->key.pageaddr );
  }
}

inline void del_prefetch_info( int ufd, uint64_t pageaddr )
{
  prefetch_info l, *p = NULL;
  l.key.ufd = ufd;
  memcpy( l.key.pageaddr, &pageaddr, sizeof(pageaddr) );
  HASH_FIND( hh3, prefetch_list, &l.key, sizeof(info_key_t), p );
  if(p!=NULL)
  {
    HASH_DELETE( hh3, prefetch_list, p );
    free(p);
  }
  else
    log_err("%s: failed to delete key %llx from prefetch_list", __func__, pageaddr);
}

inline int get_prefetch_list_size()
{
  return HASH_CNT( hh3, prefetch_list );
}

void *prefetch_thread(void * tmp);

#endif
