/*
 * Copyright 2015 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, May 2015
 */

#include <stdio.h>
#include <stdlib.h>
#include <cstdint>
#include "externRAMClient.hh"
#include "Cycles.h"

class testClient {
  public:
    externRAMClient * client;
    testClient();
    int writeTest();
    int asyncReadTest();
    int asyncMultiReadTest();
};

testClient::testClient() {
  int type = 0; //ramcloud_impl
  char locator[] = "zk:ngn2-ib:2181";
  client = externRAMClient::create(type,locator);
}

int testClient::writeTest() {
  uint64_t b,e;

  char value[100] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  int key = 0;
  int length;
  uint64_t rpc_key;

  b = RAMCloud::Cycles::rdtsc();
  client->write((uint64_t)key, (void *) value, sizeof(value));
  printf("writing to table took %lu microseconds\n", (RAMCloud::Cycles::rdtsc() - b) / 2400);

  // first read
  bool sync = true;
  b = RAMCloud::Cycles::rdtsc();
  client->multiRead((uint64_t)key,1);
  e = RAMCloud::Cycles::rdtsc();
  length = client->getSize((uint64_t)key);
  uint8_t *recvBuf = new uint8_t[length];
  if (length != client->getBuf((uint64_t)key, (void *)recvBuf)) {
    printf("Error: getbuf returned something other than %d bytes\n",length);
  }
  else {
    printf("First read of %d bytes completed in %lu microseconds\n", length, (e - b) / 2400);
    printf("Buffer = %s\n",(char *)recvBuf);
  }

  // second read
  b = RAMCloud::Cycles::rdtsc();
  client->multiRead((uint64_t)key,1);
  e = RAMCloud::Cycles::rdtsc();
  length = client->getSize((uint64_t)key);
  recvBuf = new uint8_t[length];
  if (length != client->getBuf(rpc_key, (void *)recvBuf)) {
    printf("Error: getbuf returned something other than %d bytes\n",length);
  }
  else {
    printf("Second read of %d bytes completed in %lu microseconds\n", length, (e - b) / 2400);
    printf("Buffer = %s\n",(char *)recvBuf);
  }

  // third read
  b = RAMCloud::Cycles::rdtsc();
  client->multiRead((uint64_t)key,1);
  e = RAMCloud::Cycles::rdtsc();
  length = client->getSize((uint64_t)key);
  recvBuf = new uint8_t[length];
  if (length != client->getBuf((uint64_t)key, (void *)recvBuf)) {
    printf("Error: getbuf returned something other than %d bytes\n",length);
  }
  else {
    printf("Third read of %d bytes completed in %lu microseconds\n", length, (e - b) / 2400);
    printf("Buffer = %s\n",(char *)recvBuf);
  }
  return 0;
}


int testClient::asyncReadTest() {
  uint64_t b,e;

  char value[100] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  int key = 0;
  uint64_t rpc_key;
  int length;

  // write to RAMCloud
  client->write((uint64_t)key, (void *) value, sizeof(value));

  // read from RAMCloud
  bool ready = false;
  b = RAMCloud::Cycles::rdtsc();

  bool sync = false;
  client->multiRead((uint64_t)key,1);
  while(!ready) {
    ready = client->isReady((uint64_t)key);
  }
  length = client->getSize((uint64_t)key);
  uint8_t *recvBuf = new uint8_t[length];
  length = client->getBuf((uint64_t)key, (void *)recvBuf);
  e = RAMCloud::Cycles::rdtsc();
  printf("Read of %d bytes completed in %lu microseconds\n", length, (e - b) / 2400);
  printf("Buffer = %s\n",(char *)recvBuf);

  return 0;
}

int testClient::asyncMultiReadTest() {
  uint64_t b,e;

  char value[100] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  int key = 0;
  uint64_t rpc_key;
  int length;

  // write to RAMCloud
  b = RAMCloud::Cycles::rdtsc();
  for (key = 0; key < 100; key ++) {
    client->write((uint64_t)key, (void *) value, sizeof(value));
  }
  e = RAMCloud::Cycles::rdtsc();
  printf("Writing of 100 entries took %lu microseconds\n", (e - b) / 2400);

  // read from RAMCloud
  bool sync = false;
  bool ready[100];
  for (key = 0; key < 100; key++) {
    ready[key] = false;
  }

  uint64_t reqs[100];
  b = RAMCloud::Cycles::rdtsc();
  for (key = 0; key < 100; key++) {
    reqs[key] = (uint64_t)key;
    client->multiRead((uint64_t)key,1);
  }
  for (key = 0; key < 100; key++) {
    while(!ready[key]) {
      ready[key] = client->isReady(reqs[key]);
    }
  }
  e = RAMCloud::Cycles::rdtsc();
  printf("Reading of 100 entries took %lu microseconds\n", (e - b) / 2400);

  uint8_t * buffers[100];
  for (key = 0; key < 100; key++) {
    length = client->getSize(reqs[key]);
    buffers[key] = new uint8_t[length];
    client->getBuf((uint64_t)reqs[key], (void *)buffers[key]);
    //printf("Buffer = %s\n",(char *)buffers[key]);
    delete buffers[key];
  }

  return 0;
}


int
main(int argc, char *argv[]) {
  testClient test;
  int err;

  err = test.writeTest();
  if (err)
    return 1;
  //err = test.asyncReadTest();
  //if (err)
  //  return 1;
  //err = test.asyncMultiReadTest();
  //if (err)
  //  return 1;

  return 0;
}
