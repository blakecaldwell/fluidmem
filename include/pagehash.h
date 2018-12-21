#ifndef __pagehash_h__
#define __pagehash_h__

#include <sys/user.h> /* for PAGE_MASK */

uint64_t hash_page_key(uint64_t key, int ufd) {
  key = key & (uint64_t)(PAGE_MASK);
  return key + ufd % PAGE_SIZE;
}

#endif // __pagehash_h__
