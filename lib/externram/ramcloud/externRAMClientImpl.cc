/*
 * Copyright 2015 Blake Caldwell, University of Colorado
 * Copyright 2015 Youngbin Im, University of Colorado
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, May 2015
 */

/*
 * externRAMClientImpl.cc
 *
 * This file implements a concrete class of externRAMClient that interoperates
 * with RAMCloud
*/

#include "externRAMClientImpl.hh"
#include <dbg.h>
#include "Cycles.h" // for RAMCloud timing
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <stdio.h>
#include <cstdint>  // for uint types
#include <sys/user.h> /* for PAGE_SIZE */
#include <MultiRead.h>
#include <ClientException.h>
#include <Dispatch.h>

using namespace RAMCloud;
string locator;
uint64_t upid;

/*
 *** externRAMClient::create() ****
 * This is the Pointer to Implementation Pattern
 ********************************
 */
externRAMClient * externRAMClient::create(int type, char * config, uint64_t upid_val)
{
  log_trace_in("%s", __func__);
  RAMCloud::Context context(false);
  /*
   * Necesssary to use null_deleter or object will get implicitly deleted.
   * TODO: fix this "memory" leak. Are we misusing shared_ptr here?
   * Not too worried about a memory leak since this is only one perf IPRewriter
   */
  locator = config;
  upid = upid_val;
  boost::shared_ptr<externRAMClient> impl(new externRAMClientImpl(),null_deleter());
  log_trace_out("%s", __func__);
  return (impl.get());
}


/*
 *** externRAMClientImpl::externRAMClientImpl() ***
 *  default parameterless constructor
 **********************************************
 */
externRAMClientImpl::externRAMClientImpl()
{
  log_trace_in("%s", __func__);
  /* Start up RAMCloud */
  myClient = new RamCloud(&context,locator.c_str());
#ifdef THREADED_WRITE_TO_EXTERNRAM
  myClient_write = new RamCloud(&context_write,locator.c_str());
#endif
#if defined(THREADED_PREFETCH) || defined(ASYNREAD)
  myClient_multiread = new RamCloud(&context_multiread,locator.c_str());
#endif

  snprintf(clientId, sizeof(clientId), "%llu", upid);
  log_debug("externRAMClientImpl: clientId=%s", clientId);

  /* Create the table in RAMCloud */
  tableId = createTable(clientId);
#ifdef THREADED_WRITE_TO_EXTERNRAM
  tableId_write = myClient_write->getTableId(clientId);
#endif
#if defined(THREADED_PREFETCH) || defined(ASYNREAD)
  tableId_multiread = myClient_multiread->getTableId(clientId);
#endif
  log_trace_out("%s", __func__);
}


/*
 *** externRAMClientImpl::~externRAMClientImpl() ***
 *  default parameterless constructor
 **********************************************
 */
externRAMClientImpl::~externRAMClientImpl()
{
  log_trace_in("%s", __func__);
  delete myClient;
#ifdef THREADED_WRITE_TO_EXTERNRAM
  delete myClient_write;
#endif
#ifdef THREADED_PREFETCH
  delete myClient_multiread;
#endif
  log_trace_out("%s", __func__);
}

/*
 *** externRAMClientImpl::createTable() ***
 *
 * create table in RAMCloud
 *
 @ tag: unique ID for table in RAMCloud
 -> returns: tableId (numeric index)
 **********************************************
 */
uint64_t externRAMClientImpl::createTable(const char * tag) {
  log_trace_in("%s", __func__);

  uint64_t tableID;
  try {
    tableID = myClient->getTableId(tag);
    log_debug("table already existed:%s, tableID:%lu", tag, tableID);
  } catch (RAMCloud::ClientException& e) {
    log_debug("Creating a new table:%s", tag);
    tableID = myClient->createTable(tag);
  }
  log_trace_out("%s", __func__);

  return tableID;
}

/*
 *** externRAMClientImpl::dropTable() ***
 *
 * private helper: delete a table in RAMCloud
 *
 @ tag: unique ID for table in RAMCloud
 -> void
 **********************************************
 */
void externRAMClientImpl::dropTable(const char * tag) {
  log_trace_in("%s", __func__);

  try {
    myClient->dropTable(tag);
  } catch (RAMCloud::ClientException& e) {
    log_err("RAMCloud exception: %s", e.str().c_str());
  }

  log_trace_out("%s", __func__);
}

/*
 *** externRAMClientImpl::write() ***
 *
 * write a key,value pair in RAMCloud
 *
 @ hashcode: unique key
 @ data: buffer pointing to data to be stored in RAMCloud
 @ size: length in bytes of the data to be stored
 -> returns: error code
 **********************************************
 */
int externRAMClientImpl::write(uint64_t hashcode, void *data, int size) {
  // only a single write RPC is supported
  log_trace_in("%s", __func__);
  RAMCloud::RamCloud * client = myClient;
  uint64_t tid = tableId;
  int ret = 0;
#ifdef THREADED_WRITE_TO_EXTERNRAM
  client = myClient_write;
  tid = tableId_write;
#endif

  try {
    /* write to RAMCloud */
    void * hashcode_ptr = &hashcode;
    client->write(tid, hashcode_ptr, sizeof(uint64_t), data, size);
  }
  catch (RAMCloud::ClientException& e) {
    ret = e.status;
    log_err("RAMCloud exception: %s", e.str().c_str());
  }
  catch (RAMCloud::Exception& e) {
    ret = e.errNo;
    log_err("RAMCloud exception: %s", e.str().c_str());
  }

  log_trace_out("%s", __func__);
  return ret;
}

/*
 *** externRAMClientImpl::read() ***
 *
 * read the data associated with a given key in RAMCloud
 *
 @ hashcode: unique key
 @ recvBuf:
 -> returns: length of recvBuf
 **********************************************
 */
int externRAMClientImpl::read(uint64_t hashcode, void * recvBuf) {
  log_trace_in("%s", __func__);
  int length=0;
  Buffer RCBuf;

  try {
    myClient->read(tableId, &hashcode, sizeof(uint64_t), &RCBuf);
    length = RCBuf.size();
    /* don't risk overflowing recvBuf */
    if (length > PAGE_SIZE)
      length = PAGE_SIZE;
    RCBuf.copy(0,length,recvBuf);
  } catch (RAMCloud::ClientException& e) {
    if( e.status!=STATUS_OBJECT_DOESNT_EXIST )
      log_err("RAMCloud exception: %s", e.str().c_str());
  }
  catch (RAMCloud::Exception& e) {
    log_err("RAMCloud exception: %s", e.str().c_str());
  }

  log_trace_out("%s", __func__);
  return length;
}

/*
 *** externRAMClientImpl::MultiRead() ***
 *
 * read the data associated with one key in RAMCloud and prefetch
 * additional keys
 *
 @ hashcodes: pointer to an array of unique keys
 @ num_prefetch: number of keys to prefetch with spatial locality
 @ recvBufs: pointer to an array of receive buffers
 @ lengths: pointer to an array of lengths to be recoreded by this function
 -> returns: void
 **********************************************
 */
int externRAMClientImpl::multiRead(uint64_t * hashcodes, int num_prefetch, void ** recvBufs, int * lengths) {
  log_trace_in("%s", __func__);

  RAMCloud::RamCloud * client = myClient;
  uint64_t tid = tableId;
#if defined(THREADED_PREFETCH) || defined(ASYNREAD)
  client = myClient_multiread;
  tid = tableId_multiread;
#endif
  try {
    /* Create a new container for all RPCs associated with this key */
    MultiReadObject * requests_ptr[num_prefetch];
    MultiReadObject requests[num_prefetch];
    Tub<ObjectBuffer> values[num_prefetch];
    for( int i=0; i<num_prefetch; i++ )
    {
      MultiReadObject r(tid, &hashcodes[i],
                                 sizeof(uint64_t), &values[i] );
      requests[i] = r;
      requests_ptr[i] = &(requests[i]);
    }
    client->multiRead( &requests_ptr[0], num_prefetch );
    for( int j=0; j<num_prefetch; j++ )
    {
      if( requests_ptr[j]->status != STATUS_OK )
      {
        log_err("RAMCloud error: cannot read a value for key %lx", hashcodes[j]);
        continue;
      }
      int length = values[j].get()->getObject()->getValueLength();
      recvBufs[j] = malloc(length);
      memcpy( recvBufs[j], values[j].get()->getValue(), length );
      lengths[j] = length;
    }
  }
  catch (RAMCloud::ClientException& e) {
    log_err("RAMCloud exception: %s", e.str().c_str());
  }
  catch (RAMCloud::Exception& e) {
    log_err("RAMCloud exception: %s", e.str().c_str());
  }

  log_trace_out("%s", __func__);
  return 0;
}

/*
 *** externRAMClientImpl::MultiWrite() ***
 *
 * write the data associated with multiple keys in RAMCloud
 *
 @ hashcodes: pointer to an array of unique keys
 @ num_write: number of keys to write
 @ data: pointer to an array of buffers to write
 @ lengths: pointer to an array of lengths to write
 -> returns: error code
 **********************************************
 */
int externRAMClientImpl::multiWrite(uint64_t * hashcodes, int num_write, void ** data, int * lengths ) {
  log_trace_in("%s", __func__);

  RAMCloud::RamCloud * client = myClient;
  uint64_t tid = tableId;
  int ret = 0;
#ifdef THREADED_WRITE_TO_EXTERNRAM
  client = myClient_write;
  tid = tableId_write;
#endif

  try {
    /* Create a new container for all RPCs associated with this key */
    MultiWriteObject * requests_ptr[num_write];
    Tub<MultiWriteObject> requests[num_write];
    for( int i=0; i<num_write; i++ )
    {
      requests[i].construct(tid, &hashcodes[i], sizeof(uint64_t), data[i], lengths[i]);
      requests_ptr[i] = requests[i].get();
    }
    client->multiWrite( requests_ptr, num_write );
  }
  catch (RAMCloud::ClientException& e) {
    /* ramcloud status are defined in Status.h */
    /* refer to https://ramcloud.stanford.edu/docs/doxygen/Status_8h_source.html */
    ret = e.status;
    log_err("RAMCloud exception: %s", e.str().c_str());
  }
  catch (RAMCloud::Exception& e) {
    ret = e.errNo;
    log_err("RAMCloud exception: %s", e.str().c_str());
  }

  log_trace_out("%s", __func__);
  return ret;
}

#ifdef ASYNREAD
/*
 *** externRAMClientImpl::read_top() ***
 *
 * read the data associated with a given key in RAMCloud asynchronously
 *
 @ hashcode: unique key
 @ recvBuf:
 -> returns: void
 **********************************************
 */
void externRAMClientImpl::read_top(uint64_t hashcode, void * recvBuf) {
  log_trace_in("%s", __func__);

  try {
    read_for_asynread = new ReadRpc(myClient, tableId, &hashcode, sizeof(uint64_t), &buf_for_asynread);
  } catch (RAMCloud::ClientException& e) {
    if( e.status!=STATUS_OBJECT_DOESNT_EXIST )
      log_err("RAMCloud exception: %s", e.str().c_str());
  }
  catch (RAMCloud::Exception& e) {
    log_err("RAMCloud exception: %s", e.str().c_str());
  }

  log_trace_out("%s", __func__);
}

/*
 *** externRAMClientImpl::read_bottom() ***
 *
 * read the data associated with a given key in RAMCloud asynchronously
 *
 @ hashcode: unique key
 @ recvBuf:
 -> returns: length of recvBuf
 **********************************************
 */
int externRAMClientImpl::read_bottom(uint64_t hashcode, void * recvBuf) {
  log_trace_in("%s", __func__);
  int length=0;

  try {
    while (!read_for_asynread->isReady()) {
      context.dispatch->poll();
    }
    read_for_asynread->wait();

    length = buf_for_asynread.size();
    /* don't risk overflowing recvBuf */
    if (length > PAGE_SIZE)
      length = PAGE_SIZE;
    buf_for_asynread.copy(0,length,recvBuf);
  } catch (RAMCloud::ClientException& e) {
    if( e.status!=STATUS_OBJECT_DOESNT_EXIST )
      log_err("RAMCloud exception: %s", e.str().c_str());
  }
  catch (RAMCloud::Exception& e) {
    log_err("RAMCloud exception: %s", e.str().c_str());
  }
  if( read_for_asynread )
    delete read_for_asynread;
  log_trace_out("%s", __func__);
  return length;
}

/*
 *** externRAMClientImpl::MultiRead_top() ***
 *
 * read the data associated with one key in RAMCloud and prefetch
 * additional keys
 *
 @ hashcodes: pointer to an array of unique keys
 @ num_prefetch: number of keys to prefetch with spatial locality
 @ recvBufs: pointer to an array of receive buffers
 @ lengths: pointer to an array of lengths to be recoreded by this function
 -> returns: void
 **********************************************
 */
void externRAMClientImpl::multiRead_top(uint64_t * hashcodes, int num_prefetch, void ** recvBufs, int * lengths) {
  log_trace_in("%s", __func__);

  RAMCloud::RamCloud * client = myClient;
  uint64_t tid = tableId;
  client = myClient_multiread;
  tid = tableId_multiread;
  try {
    /* Create a new container for all RPCs associated with this key */
    for( int i=0; i<num_prefetch; i++ )
    {
      MultiReadObject r(tid, &hashcodes[i],
                                 sizeof(uint64_t), &buf_for_asynmread[i] );
      requests_for_asynmread[i] = r;
      requests_ptr_for_asynmread[i] = &(requests_for_asynmread[i]);
    }
    read_for_asynmread = new MultiRead( client, &requests_ptr_for_asynmread[0], num_prefetch);
  }
  catch (RAMCloud::ClientException& e) {
    log_err("RAMCloud exception: %s", e.str().c_str());
  }
  catch (RAMCloud::Exception& e) {
    log_err("RAMCloud exception: %s", e.str().c_str());
  }

  log_trace_out("%s", __func__);
}

/*
 *** externRAMClientImpl::MultiRead_bottom() ***
 *
 * read the data associated with one key in RAMCloud and prefetch
 * additional keys
 *
 @ hashcodes: pointer to an array of unique keys
 @ num_prefetch: number of keys to prefetch with spatial locality
 @ recvBufs: pointer to an array of receive buffers
 @ lengths: pointer to an array of lengths to be recoreded by this function
 -> returns: void
 **********************************************
 */
int externRAMClientImpl::multiRead_bottom(uint64_t * hashcodes, int num_prefetch, void ** recvBufs, int * lengths) {
  log_trace_in("%s", __func__);

  RAMCloud::RamCloud * client = myClient;
  uint64_t tid = tableId;
#ifdef THREADED_PREFETCH
  client = myClient_multiread;
  tid = tableId_multiread;
#endif
  try {
    while (!read_for_asynmread->isReady()) {
      context_multiread.dispatch->poll();
    }
    read_for_asynmread->wait();
    for( int j=0; j<num_prefetch; j++ )
    {
      if( requests_ptr_for_asynmread[j]->status != STATUS_OK )
      {
        log_err("RAMCloud error: cannot read a value for key %lx", hashcodes[j]);
        continue;
      }
      int length = buf_for_asynmread[j].get()->getObject()->getValueLength();
      recvBufs[j] = malloc(length);
      memcpy( recvBufs[j], buf_for_asynmread[j].get()->getValue(), length );
      lengths[j] = length;
    }
  }
  catch (RAMCloud::ClientException& e) {
    log_err("RAMCloud exception: %s", e.str().c_str());
  }
  catch (RAMCloud::Exception& e) {
    log_err("RAMCloud exception: %s", e.str().c_str());
  }
  if( read_for_asynmread )
    delete read_for_asynmread;
  log_trace_out("%s", __func__);
  return 0;
}
#endif

/*
 *** externRAMClientImpl::MultiReadTest() ***
 *
 * test the multiRead function showing how it should be used
 *
 -> returns: void
 **********************************************
 */
void externRAMClientImpl::multiReadTest()
{
  log_trace_in("%s", __func__);
  uint64_t keys[] = { 123,456,7890 };
  char values[3][50] = { "dddddddddddddddddddddd", "fffffffffffffffffff", "gggggggggggggggg" };
  char * buf[3];
  int length[3];
  for( int i=0; i<3; i++ )
  {
    write( keys[i], values[i], strlen(values[i]) );
  }

  multiRead( keys, 3, (void**) buf, length );
  for( int j=0; j<3; j++ )
  {
    log_info("%d key %ld value %.*s legnth %d", j, keys[j], length[j], buf[j], length[j] );
    free(buf[j]); // buffer should be freed at the caller after using it
  }

  remove( keys[1] );
  multiRead( keys, 3, (void**) buf, length );
  for( int j=0; j<3; j++ )
  {
    log_info("%d key %ld value %.*s legnth %d", j, keys[j], length[j], buf[j], length[j] );
    if( buf[j] )
      free(buf[j]); // buffer should be freed at the caller after using it
  }

  log_trace_out("%s", __func__);
}

/*
 *** externRAMClientImpl::remove() ***
 *
 * remove the data associated with a given key in map
 *
 @ key: unique key
 -> returns: success on deletion
 **********************************************
 */
int externRAMClientImpl::remove(uint64_t hashcode) {
  log_trace_in("%s", __func__);
  int ret = 0;
  RAMCloud::RamCloud * client = myClient;
  uint64_t tid = tableId;

  try {
    void * hashcode_ptr = &hashcode;
    client->remove(tid, hashcode_ptr, sizeof(uint64_t));
    ret = 1;
  }
  catch (RAMCloud::ClientException& e) {
    log_err("RAMCloud exception: %s", e.str().c_str());
  }
  catch (RAMCloud::Exception& e) {
    log_err("RAMCloud exception: %s", e.str().c_str());
  }

  log_trace_out("%s", __func__);
  return ret;
}

/*
 *** externRAMClientImpl::getPerfStats() ***
 *
 * obtain the performance statistics
 *
 @ out: pointer to the stat output
 -> returns: void
 **********************************************
 */
void externRAMClientImpl::getPerfStats(RAMCloud::PerfStats* out) {
  log_trace_in("%s", __func__);
  Buffer statsBuf;
  myClient->serverControlAll(WireFormat::ControlOp::GET_PERF_STATS, NULL, 0, &statsBuf);
  WireFormat::ServerControlAll::Response* statsHdr =
    statsBuf.getStart<WireFormat::ServerControlAll::Response>();
  uint32_t startOff = sizeof32(*statsHdr);
  const char* outputBuf = reinterpret_cast<const char*>(
    statsBuf.getRange(sizeof32(*statsHdr), statsHdr->totalRespLength));
  for (uint32_t i = 0; i < statsHdr->respCount; ++i) {
    int perItemSize = sizeof(PerfStats) + sizeof(WireFormat::ServerControl::Response);
    const WireFormat::ServerControl::Response* entryRespHdr =
      reinterpret_cast<const WireFormat::ServerControl::Response*>(
        outputBuf + i * perItemSize);
    out[i] = *reinterpret_cast<const PerfStats*>(outputBuf + i * perItemSize + sizeof(*entryRespHdr));
  }
  log_trace_out("%s", __func__);
}

/*
 *** externRAMClientImpl::isFull() ***
 *
 * check whether the memory assigned for the externRAM in the server corresponding to a given key is full
 *
 @ hashcode: unique key
 -> returns: true(full) or false(not full)
 **********************************************
 */
bool externRAMClientImpl::isFull(uint64_t hashcode)
{
  log_trace_in("%s", __func__);
  PerfStats keyServerStat;
  Buffer statsBuffer;
  uint64_t mb = 1024*1024;

  myClient->objectServerControl(tableId, (void*) &hashcode, sizeof(int64_t),
                    WireFormat::ControlOp::GET_PERF_STATS, NULL, 0,
                    &statsBuffer);
  keyServerStat = *statsBuffer.getStart<PerfStats>();

  log_info( "server that contains the key %p: size %lluMB used %lluMB free %lluMB\n", hashcode,
    keyServerStat.logSizeBytes/mb, keyServerStat.logUsedBytes/mb, keyServerStat.logFreeBytes/mb );
  log_trace_out("%s", __func__);
  return (keyServerStat.logSizeBytes<=keyServerStat.logUsedBytes);
}

/*
 *** externRAMClientImpl::isFullAll() ***
 *
 * check whether the memory assigned for the externRAM in one of the servers is full
 *
 -> returns: true(full) or false(not full)
 **********************************************
 */
bool externRAMClientImpl::isFullAll()
{
  log_trace_in("%s", __func__);
  PerfStats stats[MAX_NUMBER_OF_RAMCLOUD_SERVERS];
  uint64_t mb = 1024*1024;
  bool ret = false;
  memset( stats, 0, sizeof(stats) );

  getPerfStats( stats );
  int i=0;
  for( ; i<MAX_NUMBER_OF_RAMCLOUD_SERVERS; i++ )
  {
    if( stats[i].logSizeBytes==0 )
      break;
    log_info( "server %d: size %lluMB used %lluMB free %lluMB\n",
      i, stats[i].logSizeBytes/mb, stats[i].logUsedBytes/mb, stats[i].logFreeBytes/mb );
    if( stats[i].logSizeBytes<=stats[i].logUsedBytes )
      ret = true;
  }
  log_trace_out("%s", __func__);
  return ret;
}

/*
 *** externRAMClientImpl::getUsage() ***
 *
 * return the amount of memory in each server (total. used, free)
 *
 -> returns: number of active servers
 **********************************************
 */
int externRAMClientImpl::getUsage(ServerUsage ** usage_ptr)
{
  log_trace_in("%s", __func__);
  PerfStats stats[MAX_NUMBER_OF_RAMCLOUD_SERVERS];
  uint64_t mb = 1024*1024;
  int active_servers = 0;
  memset( stats, 0, sizeof(stats) );

  getPerfStats( stats );
  int i=0;
  for( ; i<MAX_NUMBER_OF_RAMCLOUD_SERVERS; i++ )
  {
    if( stats[i].logSizeBytes==0 )
      continue;

    (*usage_ptr)[active_servers].size = stats[i].logSizeBytes/mb;
    (*usage_ptr)[active_servers].used = stats[i].logUsedBytes/mb;
    (*usage_ptr)[active_servers].free = stats[i].logFreeBytes/mb;
    active_servers++;

    // increase size by one
    (*usage_ptr) = (ServerUsage *)realloc((*usage_ptr), sizeof(ServerUsage) * (active_servers+1));
    if(!(*usage_ptr)) {
      log_err("%s: failed to increase size of usage list to %d", __func__, active_servers);
    }

  }
  log_trace_out("%s", __func__);
  return active_servers;
}
