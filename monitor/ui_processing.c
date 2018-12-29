/*
 * Copyright 2015 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, May 2015
 */

#include <affinity.h>  /* should be the first to be included */
#include "ui_processing.h"

/* FluidMem includes */
#include <userfault.h>
#include <dbg.h>
#ifdef MONITORSTATS
#include <monitorstats.h>
#ifdef TIMING
#include <timingstats.h>
#endif
#endif

/* cstdlib includes */
#include <sys/user.h>
#include <stdbool.h>
#include <stdint.h>      /* for uint64_t */
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/syscall.h> /* for __NR_userfaultfd */
#include <unistd.h>      /* for syscall */
#include <stdio.h>
#include <fcntl.h>       /* for O_CLOEXEC */
#include <errno.h>
#include <string.h>      /* for __func__ */
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <externRAMClientWrapper.h>
#include <LRUBufferWrapper.h>
#include <PageCacheWrapper.h>

extern struct externRAMClient *client;
extern struct LRUBuffer *lru;
extern struct PageCache * pageCache;

#define MENU_EVICT 1
#define MENU_RESIZE 2
#define MENU_DISCONNECT 3
#define MENU_DUMP 4

extern int stop_by_ui;
int isDisconnected = 0;

#ifdef MONITORSTATS
#ifdef TIMING
uint32_t max_bucket_slots;
char ** reverse_buckets;
uint32_t buckets_mask;
TimingBucket * timing_buckets;
#endif
#endif

void signal_callback_handler(int signum)
{
  isDisconnected = 1;
}

void *ui_processing_thread(void * tmp)
{
  setThreadCPUAffinity(CPU_FOR_UI_THREAD, "ui_processing_thread", TID());
  int listenfd = 0, connfd = 0;
  struct sockaddr_in serv_addr, client_addr;
  char recvBuff[1024];
  time_t ticks;
  int port = 5001;
  char peer_addr_str[ INET_ADDRSTRLEN ];
  char date[MAX_DATESTAMP_LEN];
  struct timeval tv;
  struct tm *tm;

  /* Catch Signal Handler SIGPIPE */
  signal(SIGPIPE, signal_callback_handler);

  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  int optval = 1;
  setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

  memset(&serv_addr, '0', sizeof(serv_addr));

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  serv_addr.sin_port = htons(port);

  bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
  listen(listenfd, 10);

  while(1)
  {

    connfd = accept(listenfd, (struct sockaddr*)NULL, NULL);

    inet_ntop( AF_INET, &client_addr, peer_addr_str, INET_ADDRSTRLEN );

    log_debug("%s: connection from %s", __func__, peer_addr_str);

    FILE * out = fdopen(connfd, "w");

    int done = 0;
    while(!done)
    {
      int n=0;

      memset(recvBuff, 0, sizeof(recvBuff));
      n = read(connfd, recvBuff, sizeof(recvBuff) );

      char *token, *string, *tofree;
      int menu=0;

      tofree = string = strdup(recvBuff);

      while ((token = strsep(&string, " \n")) != NULL)
      {
        if( menu == MENU_EVICT )
        {
          int size = atoi(token);
          evict_to_externram_multi( size );
          fflush(out);
          done = 1;
          break;
        }
        else if( menu == MENU_RESIZE )
        {
          int size = atoi(token);
          resizeLRUBuffer( size );
          fflush(out);
          done = 1;
          break;
        }
        else if( menu == MENU_DISCONNECT )
        {
          int pid = atoi(token);
          if (removePid((uint32_t)pid) < 0)
            fprintf(out, "error removing pid %u\n", (uint32_t)pid);

          fflush(out);
          done = 1;
          break;
        }
#ifdef TIMING
        else if( menu == MENU_DUMP )
        {
          FILE *file = fopen( token, "w+" );
          if (!file)
            fprintf(out, "failed to open file %s\n", token);
          else {
           StatsDumpBuckets(file);
           fflush(file);
           if (fclose(file) != 0)
              fprintf(out, "failed to close file\n");
          }

          fflush(out);
          done = 1;
          break;

        }
#endif
        else if( strcmp(token,"print")==0 || strcmp(token,"p")==0 )
        {
          if (!print_info)
            print_info = 1;
          else
            print_info = 0;
          fprintf(out, "print_info now set to %s", print_info ? "true" : "false");
          done = 1;
          fflush(out);
          break;
        }
        else if( strcmp(token,"flush")==0 || strcmp(token,"f")==0 )
        {
          int * ufd_list = malloc(1 * sizeof(int));
          int ** ufd_list_ptr = &ufd_list;
          int num_dead_fds = 0;

          num_dead_fds = purgeDeadUpids(ufd_list_ptr);
          free(*ufd_list_ptr);
          fflush(out);
          break;
        }
        else if( strcmp(token,"listpids")==0 || strcmp(token,"l")==0 )
        {
          uint32_t * pid_list = malloc(1 * sizeof(uint32_t));
          uint32_t ** pid_list_ptr = &pid_list;
          int num_pids = 0;
          int i = 0;

          num_pids = listPids(pid_list_ptr);
          fprintf(out, "pids registered to this monitor:\n");
          for(i = 0; i < num_pids; i++) {
            fprintf(out, "%u\n", (*pid_list_ptr)[i]);
          }
          free(*pid_list_ptr);
          fflush(out);
          break;
        }
        else if( strcmp(token,"usage")==0 || strcmp(token,"u")==0 )
        {
          ServerUsage * usage = malloc(1 * sizeof(ServerUsage));
          ServerUsage ** usage_ptr = &usage;

          int num_servers = getExternRAMUsage(usage_ptr);
          if (num_servers > 0 && ((*usage_ptr) != NULL)) {
            int i = 0;

            fprintf(out, "%14s: %8s %8s %8s\n", "server usage", "used_MB", "total_MB", "free_MB" );
            for(i = 0; i < num_servers; i++) {
              fprintf(out, "%14d  %8u %8u %8u\n", i, (*usage_ptr)[i].used, (*usage_ptr)[i].size, (*usage_ptr)[i].free);
            }
            free(*usage_ptr);
          }
          fflush(out);
          break;
        }
        else if( strcmp(token,"evict")==0 || strcmp(token,"e")==0 )
        {
          menu = MENU_EVICT;
          continue;
        }
        else if( strcmp(token,"resize")==0 || strcmp(token,"r")==0 )
        {
          menu = MENU_RESIZE;
          continue;
        }
        else if( strcmp(token,"disconnectpid")==0 || strcmp(token,"d")==0 )
        {
          menu = MENU_DISCONNECT;
          continue;
        }
#ifdef MONITORSTATS
        else if(strcmp(token,"stat")==0 || strcmp(token,"s")==0 )
        {
          fprintf(out,"LRU Buffer Size:\t%lu\n", StatsGetStat(LRU_BUFFERSIZE));
          fprintf(out,"LRU Buffer Capacity:\t%lu\n", StatsGetStat(LRU_BUFFERCAP));
          fprintf(out,"Registered ufds:\t%lu\n", StatsGetStat(NUM_UFDS));
          fprintf(out,"Total Page Fault Count:\t%lu\n", StatsGetStat(PAGE_FAULTS));
          fprintf(out,"Zero Page Count:\t%lu\n", StatsGetStat(ZEROPAGES));
          fprintf(out,"Placed Data Page Count:\t%lu\n", StatsGetStat(PLACED_PAGES));
          fprintf(out,"Page Eviction Count:\t%lu\n", StatsGetStat(PAGES_EVICTED));
          fprintf(out,"Cache Hit Count:\t%lu\n", StatsGetStat(CACHE_HIT));
          fprintf(out,"Cache Miss Count:\t%lu\n", StatsGetStat(CACHE_MISS));
          fprintf(out,"Cache Hit Percentage:\t%lu\n", StatsGetStat(CACHE_HITRATIO));
          fprintf(out,"Writes Avoided:\t\t%lu\n", StatsGetStat(WRITES_AVOIDED));
          fprintf(out,"Zero Pages Left:\t%lu\n", StatsGetStat(WRITES_SKIPPED_ZERO));
          fprintf(out,"Invalid Pages Dropped:\t%lu\n", StatsGetStat(WRITES_SKIPPED_INVALID));
          fprintf(out,"Page Fault Rate:\t%f\n", StatsGetRate());

	  tv = StatsGetLastFaultTime();
	  if((tm = localtime(&tv.tv_sec)) != NULL) {
	    strftime(date, 20, "%Y-%m-%dT%H:%M:%S", tm);
	    fprintf(out, "Last Page Fault:\t[%s.%03d]\n", date, tv.tv_usec / 1000);
	  }

          fflush(out);
          break;
        }
        else if(strcmp(token,"clearstats")==0 || strcmp(token,"c")==0 )
        {
          StatsClear();
          fprintf(out, "Statistics have been cleared.\n");
          fflush(out);
          break;
        }
#ifdef TIMING
        else if(strcmp(token,"buckets")==0 || strcmp(token,"b")==0 )
        {
          StatsPrintBuckets(out);
          fflush(out);
        }
        else if(strcmp(token,"dumplatency")==0 || strcmp(token,"a")==0 )
        {
          menu = MENU_DUMP;
          continue;
        }
#endif
#endif
        else if( strcmp(token,"stop")==0 || strcmp(token,"t")==0 )
        {
          fprintf(out,"monitor stopped.\n");
          fflush(out);
          sleep(1);
          free(tofree);
          fclose(out);
          stop_by_ui = 1;
          raise(SIGINT);
          return;
        }
        else
        {
          fprintf(out,"Incorrect Command. Use 'help' for help.\n");
          fflush(out);
          break;
        }
      }
      free(tofree);
      printf( "\n" );
      if( isDisconnected ) {
        break;
        log_debug("%s: disconnected", __func__);
      }
    }
    fclose(out);
  }
}
