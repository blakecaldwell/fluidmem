/*
 * Copyright 2015 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, May 2015
 */

/*
 * LRUBufferImpl.hh
 *
 * This defines the implementation class of the interface LRUBuffer
*/


#ifndef _LRUBUFFERIMPL_H_
#define _LRUBUFFERIMPL_H_
#include <LRUBuffer.hh>
#include <boost/unordered_map.hpp>    // for hash of RPCData entries
#include <boost/container/vector.hpp> // for list of RPCRecords
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <iterator>

using namespace boost::multi_index;

#define LOOKAHEAD_SIZE 4
int cache_size = 20000;

typedef struct cache_node
{
    uint64_t hashcode;
    int ufd;
    bool operator==(const cache_node& e)const{return hashcode==e.hashcode;}
    bool operator<(const cache_node& e)const{return hashcode<e.hashcode;}
    const uint64_t & _hashcode() const { return hashcode; }
    struct ByHashcode {};
} cache_node;

class lru_list
{
	typedef multi_index_container<
		cache_node,
		indexed_by<
			sequenced<>,
			hashed_unique<
				tag<cache_node::ByHashcode>,
				 const_mem_fun<
					cache_node, const uint64_t&, &cache_node::_hashcode
				>
			>
		>
	> item_list;

public:
  typedef item_list::iterator iterator;
  typedef item_list::index<cache_node::ByHashcode>::type List;

  lru_list():max_num_items(cache_size){}

  void insert(const cache_node& item)
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
  void printCache(const char * info, const char * func, FILE * file=NULL)
  {
    FILE * out=NULL;
    if( file!=NULL )
      out = file;
    else
      out = stderr;
    fprintf(out,"%s: %s %s\n", __func__, info, func);
    iterator itr = begin();
    int i=0;
    for( ; itr!= end(); itr++, i++ )
    {
        if (i > 99)
          continue;
        fprintf(out, "%d : key=%lx\n", i, itr->hashcode);
    }
    itr--;
    i--;
    if (i > 99)
    {
        fprintf(out, "%d : key=%lx\n", i, itr->hashcode);
    }
  }
  void pop_back() {il.pop_back();}
  int getSize() {return il.size();}
  int setSize(int size) {max_num_items=size; return max_num_items;}
  int getMaxSize() {return max_num_items;}

  List::iterator find( uint64_t key ) {return il.get<cache_node::ByHashcode>().find(key);}
  iterator begin() {return il.begin();}
  iterator end() {return il.end();}
  cache_node back() {return il.back();}
  List::iterator findEnd() {return il.get<cache_node::ByHashcode>().end();}

  void erase( uint64_t key ) {
    List::iterator it = find(key);
    if( it!=findEnd() )
      il.get<cache_node::ByHashcode>().erase(it);
  }

private:
  item_list   il;
  std::size_t max_num_items;
};

// we use this to replace some detructors.
struct null_deleter
{
    void operator()(void const *) const
    {
    }
};

class LRUBufferImpl: public LRUBuffer
{
private:
    class PageInfo {
public:
      int ref_count;
    };

    lru_list cache;

public:
    virtual ~LRUBufferImpl();
    LRUBufferImpl();

    virtual void        insertCacheNode(uint64_t key, int ufd);
    virtual void        referenceCachedNode(uint64_t key, int ufd);
    virtual uint64_t    popLRU(void);
    virtual struct c_cache_node getLRU();
    virtual int         isLRUSizeExceeded(void);
    virtual int         getSize();
    virtual int         getMaxSize();
    virtual int         setSize(int size);
    virtual uint64_t *  removeUFDFromLRU(int ufd, int *numPages);
    virtual void        printLRUBuffer(FILE * file=NULL);
};
#endif
