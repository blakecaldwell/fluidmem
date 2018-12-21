#ifndef __upid_h__
#define __upid_h__

#include <dbg.h>
#include <uthash.h>  /* for fd and upid mapping */

pthread_mutex_t fdUpidMap_lock;

struct map_struct {
    int fd;
    uint64_t upid;
    struct externRAMClient *client;
    UT_hash_handle hh; /* makes this structure hashable */
};
struct map_struct *fdUpidMap= NULL;

void add_upid_in_map( int fd, uint64_t upid )
{
  log_trace_in("add_upid_in_map");

  struct map_struct *s;

  log_lock("%s: locking fdUpidMap_lock", __func__);
  pthread_mutex_lock(&fdUpidMap_lock);
  log_lock("%s: locked fdUpidMap_lock", __func__);

  s = (struct map_struct *) malloc(sizeof(struct map_struct));
  s->fd = fd;
  s->upid = upid;
  s->client = NULL;
  HASH_ADD_INT( fdUpidMap, fd, s );

  log_lock("%s: unlocking fdUpidMap_lock", __func__);
  pthread_mutex_unlock(&fdUpidMap_lock);
  log_lock("%s: unlocked fdUpidMap_lock", __func__);

  log_debug("%s: added upid %llx with fd %d to map", __func__, upid, fd);
  log_trace_out("add_upid_in_map");
}

inline uint64_t get_upid_by_fd(int fd)
{
  log_trace_in("get_upid_by_fd");

  struct map_struct *s;

  log_lock("%s: locking fdUpidMap_lock", __func__);
  pthread_mutex_lock(&fdUpidMap_lock);
  log_lock("%s: locked fdUpidMap_lock", __func__);

  HASH_FIND_INT( fdUpidMap, &fd, s );

  log_lock("%s: unlocking fdUpidMap_lock", __func__);
  pthread_mutex_unlock(&fdUpidMap_lock);
  log_lock("%s: unlocked fdUpidMap_lock", __func__);

  log_trace_out("get_upid_by_fd");

  if(s==NULL)
    return 0;
  else
    return s->upid;
}

struct externRAMClient * get_client_by_fd(int fd)
{
  log_trace_in("get_client_by_fd");

  struct map_struct *s;

  log_lock("%s: locking fdUpidMap_lock", __func__);
  pthread_mutex_lock(&fdUpidMap_lock);
  log_lock("%s: locked fdUpidMap_lock", __func__);

  HASH_FIND_INT( fdUpidMap, &fd, s );

  log_lock("%s: unlocking fdUpidMap_lock", __func__);
  pthread_mutex_unlock(&fdUpidMap_lock);
  log_lock("%s: unlocked fdUpidMap_lock", __func__);

  log_trace_out("get_client_by_fd");

  if(s==NULL)
    return NULL;
  else
    return s->client;

}

void del_fd_upid_map() {
  log_trace_in("del_fd_upid_map");

  struct map_struct *current, *tmp;

  log_lock("%s: locking fdUpidMap_lock", __func__);
  pthread_mutex_lock(&fdUpidMap_lock);
  log_lock("%s: locked fdUpidMap_lock", __func__);

  HASH_ITER(hh, fdUpidMap, current, tmp) {
    HASH_DEL(fdUpidMap,current);  /* delete; users advances to next */
    free(current);            /* optional- if you want to free  */
  }

  log_lock("%s: unlocking fdUpidMap_lock", __func__);
  pthread_mutex_unlock(&fdUpidMap_lock);
  log_lock("%s: unlocked fdUpidMap_lock", __func__);

  log_trace_out("del_fd_upid_map");
}

int del_fd_from_map(int fd)
{
  log_trace_in("del_fd_from_map");

  /*
   * No lock taken here because it is called from purgeDeadUpids
   * in userfault.c with the lock already held
   */
  struct map_struct *s;

  HASH_FIND_INT( fdUpidMap, &fd, s );
  if(s==NULL)
    return -1;
  else {
    HASH_DEL(fdUpidMap,s);
    free(s);
  }

  log_trace_out("del_fd_from_map");
  return 0;
}

int register_with_externram(char * config, int fd) {
  log_trace_in("register_with_externram");

  int ret = 0;
  int type = ramcloud_impl;
  struct map_struct *s;

  log_lock("%s: locking fdUpidMap_lock", __func__);
  pthread_mutex_lock(&fdUpidMap_lock);
  log_lock("%s: locked fdUpidMap_lock", __func__);

  HASH_FIND_INT( fdUpidMap, &fd, s );
  if(s==NULL)
    ret = -1;
  else
  {
    s->client = newExternRAMClient(type,config,s->upid);
    if (!s->client) {
      log_err("registering_with_externram");
      ret = -1;
    }
  }

  log_lock("%s: unlocking fdUpidMap_lock", __func__);
  pthread_mutex_unlock(&fdUpidMap_lock);
  log_lock("%s: unlocked fdUpidMap_lock", __func__);

  log_trace_out("register_with_externram");
  return ret;
}

uint32_t jenkins_hash(char *key, size_t len)
{
    uint32_t hash, i;
    for(hash = i = 0; i < len; ++i)
    {
        hash += key[i];
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}

uint16_t get_node_id() {
   FILE *fp;
   char buff[255];
   int strSize=0;
   uint32_t nodeId=0;

   /* Using /etc/machine-id means that every container running on a single node
    * will share the same node ID. However we don't see this as a problem under
    * the assumption that there are only one monitor container running per node. */
   fp = fopen("/etc/machine-id", "r");
   fgets(buff, 255, (FILE*)fp);
   strSize = strlen(buff);
   fclose(fp);

   nodeId = jenkins_hash(buff,strSize);
   return ((uint16_t) (((nodeId<<16) ^ (nodeId))>>16));
}

#endif // __upid_h__
