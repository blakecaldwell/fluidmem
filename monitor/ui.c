/*
 * Copyright 2015 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, May 2015
 */

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <dbg.h>

#define BUFSIZE 1024*10

void printCommand(FILE *file)
{
  fprintf( file, "help(h) : display help messages\n" );
  fprintf( file, "print(p) : toggle printing informational messages in log\n" );
  fprintf( file, "resize(r) [number] : resize the LRU buffer to the specified value\n" );
  fprintf( file, "evict(e) [number] : evict the specified number of pages from LRU buffer\n" );
  fprintf( file, "disconnectpid(d) [pid] : disconnect the specified PID from monitor and flush data from buffers\n" );
  fprintf( file, "flush(f) : flush entries in LRU for dead processes\n" );
  fprintf( file, "listpids(l) : list PIDs in for this monitor\n" );
  fprintf( file, "usage(u) : externram server usage\n" );
#ifdef MONITORSTATS
  fprintf( file, "stat(s) : display monitor stats\n" );
  fprintf( file, "clear(c) : clear monitor stats\n" );
#ifdef TIMING
  fprintf( file, "buckets(b) : print timing buckets\n" );
  fprintf( file, "dump(a) : print out latency data in buckets\n" );
#endif
#endif
  fprintf( file, "stop(t) : stop the monitor\n" );
  fprintf( file, "quit(q) : exit to the shell\n" );
}

void help()
{
  fprintf( stderr, "-------------------------------------------------------------------------\n" );
  fprintf( stderr, "FluidMem user interface \n" );
  fprintf( stderr, "-------------------------------------------------------------------------\n" );
  printCommand(stderr);
  fprintf( stderr, "-------------------------------------------------------------------------\n" );
}


void print_usage() {
  printf("Usage: ui [options] address [command]\n");
  printf("    Options: \n");
  printf("\t -i : interactive mode\n");
  printf("    Command (valid only in non-interactive mode)\n");
  printCommand(stdout);
}

int interactive_prompt(char * addr) {

  help();

  while (1)
  {

    int sockfd = 0, n = 0;

    char recvBuff[BUFSIZE];
    struct sockaddr_in serv_addr;
    int port=5001;

    memset(recvBuff, '0',sizeof(recvBuff));
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
      log_err("%s: could not create socket", __func__);
      return 1;
    }

    memset(&serv_addr, '0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if(inet_pton(AF_INET, addr, &serv_addr.sin_addr)<=0)
    {
      log_err("%s: inet_pton error occured", __func__);
      return 1;
    }

    if( connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
      log_err("%s: connect Failed", __func__);
      return 1;
    }

    printf( ">> " );

    char command[1024];
    char * c = fgets( command, 1024, stdin );
    int len = strlen(command);
    char *token, *string, *tofree;

    tofree = string = strdup(command);

    token = strsep(&string, " \n");
    if( strcmp( token, "q" )==0 || strcmp( token, "quit" )==0 )
    {
      free(tofree);
      break;
    }
    else if( strcmp( token, "h" )==0 || strcmp( token, "help" )==0 )
    {
      help();
    }
    else
    {
      if (write(sockfd, command, len) < 0) {
        log_err("%s: write Failed", __func__);
      }

      if((n = read(sockfd, recvBuff, BUFSIZE)) < 0) {
        log_err("%s: read Failed", __func__);
      }

      if( n>0 )
      {
        recvBuff[n]='\0';
        printf( "Response received. size:%d\n%s\n", n, recvBuff );
      }
    }
    free(tofree);
    close(sockfd);
  }
}

int main(int argc, char *argv[])
{

  char * addr;
  int interactive = 0;
  char * command = NULL;
  char * command_arg = NULL;

  // Parse cmd line args
  if (argc < 2) {
    print_usage();
    return 1;
  }

  if (strcmp(argv[1], "-i") == 0) {
    if (argc >= 3) {
      interactive = 1;
      addr = argv[2];
    }
    else {
      print_usage();
      return 1;
    }
  }
  else {
    addr = argv[1];
    if(argc>2) {
      command = argv[2];
      command_arg = argv[3];
    }
  }


  if (interactive == 1)
  {
    interactive_prompt(addr);
  }
  else
  {

    int sockfd = 0, n = 0;

    char recvBuff[BUFSIZE];
    struct sockaddr_in serv_addr;
    int port=5001;

    // parse the command


    memset(recvBuff, 0, sizeof(recvBuff));
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
      log_err("%s: could not create socket", __func__);
      return 1;
    }

    memset(&serv_addr, '0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if(inet_pton(AF_INET, addr, &serv_addr.sin_addr)<=0)
    {
      log_err("%s: inet_pton error occured", __func__);
      return 1;
    }

    if( connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
      log_err("%s: connect failed", __func__);
      return 1;
    }

    if (command_arg)
      snprintf(recvBuff, strlen(command) + strlen(command_arg) + 2, "%s %s", command, command_arg);
    else
      strncpy(recvBuff, command, strlen(command));

    int length = strlen(recvBuff);
    if (write(sockfd, recvBuff, length) < 0) {
      log_err("%s: write failed", __func__);
    }

    memset(recvBuff, 0, BUFSIZE);
    if((n = read(sockfd, recvBuff, BUFSIZE)) < 0) {
      log_err("%s: read failed", __func__);
    }

    if( n>0 )
    {
      recvBuff[n]='\0';
      printf( "Response received. size:%d\n%s\n", n, recvBuff );
    }
    close(sockfd);
  }

  return 0;
}

