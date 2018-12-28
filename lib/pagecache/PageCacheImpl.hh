/*
 * Copyright 2015 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, May 2015
 */

/*
 * PageCacheImpl.hh
 *
 * This defines the implementation class of the interface PageCache
*/


#ifndef _PAGECACHEIMPL_H_
#define _PAGECACHEIMPL_H_
#include <PageCache.hh>
#include <userfault.h>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/unordered_map.hpp>    // for hash of RPCData entries
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <iterator>
#include <../externram/externRAMClientWrapper.h>
#include <../lrubuffer/LRUBufferWrapper.h>
#include <../lrubuffer/LRUBuffer.hh>

using namespace boost::multi_index;

int page_cache_size = 1000;
int prefetch_size = 10;
int enable_prefetch = 0;

typedef struct page_cache_node
{
    uint64_t hashcode; // virtual address
    int fd;
    void * address; // address that the page is really stored
    int size;

    struct ByHashcodeAndFd {};
    struct AddressChange : public std::unary_function<page_cache_node,void> {
        void * p; AddressChange(void * &_p) : p(_p) {}
        void operator()(page_cache_node & r) { r.address = p; }
    };
    struct SizeChange : public std::unary_function<page_cache_node,void> {
        int p; SizeChange(int &_p) : p(_p) {}
        void operator()(page_cache_node & r) { r.size = p; }
    };
} page_cache_node;

typedef multi_index_container<
  page_cache_node,
  indexed_by<
    sequenced<>,
    hashed_unique<
      tag<page_cache_node::ByHashcodeAndFd>,
      composite_key<
        page_cache_node,
        member<page_cache_node, uint64_t, &page_cache_node::hashcode>,
        member<page_cache_node, int, &page_cache_node::fd>
      >
    >
  >
> page_item_list;

typedef page_item_list::iterator iterator;
typedef page_item_list::index<page_cache_node::ByHashcodeAndFd>::type List;

class page_cache_lru_list
{
public:

  page_cache_lru_list():max_num_items(page_cache_size){}

  void insert(const page_cache_node& item)
  {
    std::pair<iterator,bool> p=il.push_front(item);

    if(!p.second){                     /* duplicate item */
      il.relocate(il.begin(),p.first); /* put in front */
    }
  }
  bool isSizeExceeded()
  {
    return il.size()>max_num_items;
  }
  void printCache()
  {
    iterator itr = begin();
    int i=0;
    for( ; itr!= end(); itr++, i++ )
    {
        if (i > 99)
          continue;
        fprintf( stderr, "%d : key=%lx actual address=%lx size=%d fd=%d\n",
                i, itr->hashcode, (uint64_t) itr->address, itr->size, itr->fd);
    }
    itr--;
    i--;
    if (i > 99)
    {
        fprintf( stderr, "%d : key=%lx actual address=%lx size=%d fd=%d\n",
                i, itr->hashcode, (uint64_t) itr->address, itr->size, itr->fd);
    }
  }
  void pop_back() {il.pop_back();}

  List::iterator find( uint64_t key, int fd )
  {
     boost::tuple<uint64_t,int> t(key,fd);
     List::iterator it = il.get<page_cache_node::ByHashcodeAndFd>().find(t);
     return it;
  }
  iterator begin() {return il.begin();}
  iterator end() {return il.end();}
  page_cache_node back() {return il.back();}
  List::iterator findEnd() {
    return il.get<page_cache_node::ByHashcodeAndFd>().end();
  }
  void erase( uint64_t key, int fd ) {
    List::iterator it = find(key,fd);
    if( it!=findEnd() )
      il.get<page_cache_node::ByHashcodeAndFd>().erase(it);
  }

  void modify( uint64_t hashcode, void * addr, int size, int fd ) {
    boost::tuple<uint64_t,int> t(hashcode,fd);
    auto & index = il.get<page_cache_node::ByHashcodeAndFd>();
    auto it = index.find( t );
    index.modify( it, page_cache_node::AddressChange(addr));
    index.modify( it, page_cache_node::SizeChange(size));
  }
  int getSize() {return il.size();}

private:
  page_item_list il;
  std::size_t max_num_items;
};

// we use this to replace some detructors.
struct null_deleter
{
    void operator()(void const *) const
    {
    }
};

// These represent where the most recent page versions are.
// If a page is in the application, page cache, and externram, this
// should be set to OWNERSHIP_APPLICATION since the application can
// modify the page.
#define OWNERSHIP_APPLICATION 1
#define OWNERSHIP_PAGE_CACHE  2
#define OWNERSHIP_EXTERNRAM   3

class PageCacheImpl: public PageCache
{
private:
    class PageInfo {
public:
      int ref_count;
      int ownership;
      int is_zeropage; // valid only when the page is stored
                       // in externram (ownership is OWNERSHIP_EXTERNRAM)
    };

    // hash structure that holds map of PageInfo indexed by hash key
    typedef boost::unordered_map<std::string, boost::shared_ptr<PageInfo> > page_hash;
    page_hash pagehash;

    page_cache_lru_list pageCache;

    uint64_t keys_for_mread[MAX_MULTI_READ];
    void * bufs_for_mread[MAX_MULTI_READ];
    int lengths_for_mread[MAX_MULTI_READ];
    int numPrefetch;
    uint64_t g_start;
    uint64_t g_end;

    void        insertPageCacheNode(uint64_t key,int fd,void * addr,int size);
    void        referencePageCachedNode(uint64_t key,int fd);
    uint64_t    popLRU(void);
    int         isLRUSizeExceeded(void);

    void        addPageHashNode( uint64_t hashcode, int fd, int ownership );
    void        changeOwnership( uint64_t hashcode, int fd, int ownership, bool is_zeropage );
    void        changeOwnershipWithItr( page_hash::iterator itr, int ownership, bool is_zeropage );

    void        storePageInPageCache(uint64_t hashcode, int fd, void * buf, int length);
    void        storePagesInPageCache(uint64_t * hashcodes, int fd, int num_pages, char ** bufs, int * lengths);
public:
    virtual ~PageCacheImpl();
    PageCacheImpl();
    void cleanup();

    virtual int  readPageIfInPageCache( uint64_t hashcode, int fd, void ** buf );
    virtual void  readPageIfInPageCache_top( uint64_t hashcode, int fd, void ** buf );
    virtual int  readPageIfInPageCache_bottom( uint64_t hashcode, int fd, void ** buf );
    virtual void updatePageCacheAfterWrite( uint64_t hashcode, int fd, bool zeroPage );
    virtual void invalidatePageCache( uint64_t hashcode, int fd );
    virtual uint64_t * removeUFDFromPageCache( int fd, int * numPages );
    virtual uint64_t * removeUFDFromPageHash( int fd, int * numPages );
};
#endif
