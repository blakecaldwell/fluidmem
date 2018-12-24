#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/user.h> /* for PAGE_SIZE */

#include "userfault-client.h"
#include <dbg.h>

// globals for threaded tests
int *results;
char * cmp_bytes;
int ufd;
int num_pages;

int num_allocations;
pthread_barrier_t finish_barrier;

void print_usage(void) {
    printf("\tUsage: test_cases [case_num]\n\tcase_num 1-4\n");
}

typedef struct _args {
  int num_pages;
  int num_threads;
  int cycles;
  int split;
  char *cmp_bytes;
  char *arr;
  int threadindex;
  int *results;
} ThreadArgs;

int rand_lim(int limit) {
  int divisor = RAND_MAX/(limit+1);
  int retval;

  do {
    retval = rand() / divisor;
  } while (retval > limit);

  return retval;
}

int fault_test(int num_pages, int unique_writes, int cycles) {

    char * arr_region;
    int ufd;
    int ret = 0;
    int arr_size = PAGE_SIZE * num_pages; // region size in bytes
    printf("TID %d: Page size = %d bytes, Num pages = %d, Region size = %d KB, Cycles = %d, Unique writes = %d\n",
           syscall(SYS_gettid), PAGE_SIZE, num_pages, arr_size/1024, cycles, unique_writes);

    // Allocate regions of length (num_pages * PAGE_SIZE))
    arr_region = (char*)allocate_userfault(&ufd, arr_size);
    if (arr_region == NULL) {
        fprintf(stderr, "failed to allocate userfault\n");
        return -1;
    }

    // Create array to be compared against
    // Allocating on heap to avoid stack overflows on large arrays
    char* cmp_bytes =  (char *) malloc(arr_size);

    int c = 0;
    while (c < cycles) {
        // Compare unique writes
        char val = (char) rand_lim(255);
        int i;
        for (i = 0; i < unique_writes; i++) {
            int page_index = i % num_pages;
            arr_region[page_index * PAGE_SIZE] = val;
            cmp_bytes[page_index * PAGE_SIZE] = val;
        }
        for (i = 0; i < unique_writes; i++) {
            int page_index = i % num_pages;
            if(memcmp(&arr_region[page_index * PAGE_SIZE], &cmp_bytes[page_index * PAGE_SIZE], sizeof(char))!= 0) {
                fprintf(stderr, "memcmp didnt match for index %d/%d: got %d, expected %d\n", i, unique_writes,
                        (int) arr_region[page_index * PAGE_SIZE], (int) cmp_bytes[page_index * PAGE_SIZE]);
                ret = -1;
            }
        }
        if (ret < 0)
            break;
        c++;
    }

    pthread_barrier_wait(&finish_barrier);

    // if we cleanup too early, we may get EINVAL when monitor tries evicting
    // since this thread is woken up on ZERO or COPY and eviction happens
    // asynchronously
    sleep(1);

    // Cleanup
    int rc = disable_ufd_area(ufd, (void *)arr_region, arr_size);
    if (rc < 0)
        fprintf(stderr, "%s: failed to disable ufd area\n", __func__);
    close(ufd);
    free(cmp_bytes);

    return ret;
}

void *write_arr (void *args) {
    ThreadArgs *my_args = args;
    int my_num_pages = my_args->num_pages;
    int my_num_threads = my_args->num_threads;
    int my_split = my_args->split;
    char * my_arr = my_args->arr;
    char * my_cmp_bytes = my_args->cmp_bytes;
    int threadindex = my_args->threadindex;
    int end, page;

    if (threadindex + 1 == my_num_threads)
        end = my_num_pages;
    else
        end = threadindex * my_split + my_split;

    char c = rand_lim(255);
    for (page = threadindex * my_split; page < end; page++) {
        my_arr[page * PAGE_SIZE] = c;
        my_cmp_bytes[page * PAGE_SIZE] = c;
    }
}

void *verify_arr (void *args) {
    ThreadArgs *my_args = args;
    int my_num_pages = my_args->num_pages;
    int my_num_threads = my_args->num_threads;
    int my_split = my_args->split;
    char *my_arr = my_args->arr;
    char *my_cmp_bytes = my_args->cmp_bytes;
    int threadindex = my_args->threadindex;
    int *my_results = &my_args->results[threadindex];
    int end, page;

    if (threadindex + 1 == my_num_threads)
        end = num_pages;
    else
        end = threadindex * my_split + my_split;

    for (page = threadindex * my_split; page < end; page++) {
        if(memcmp(&my_arr[page * PAGE_SIZE], &my_cmp_bytes[page * PAGE_SIZE], sizeof(char))!= 0) {
            fprintf(stderr, "thread %d: memcmp didnt match for page %d/%d: got %d, expected %d\n", threadindex,
                    page, end, (int) my_arr[page * PAGE_SIZE], (int) my_cmp_bytes[page * PAGE_SIZE]);
            *my_results = -1;
        }
    }

    return;
}

void *threaded_fault_test(void *args) {
    ThreadArgs *my_args = args;
    int my_num_pages = my_args->num_pages;
    int my_cycles = my_args->cycles;
    int num_threads = my_args->num_threads;
    int ret = 0;
    int my_ufd;

    int my_arr_size = PAGE_SIZE * my_num_pages; // region size in bytes
    printf("TID %d: Page size = %d bytes, Num pages = %d, Region size = %d KB, Cycles = %d\n",
           syscall(SYS_gettid), PAGE_SIZE, num_pages, my_arr_size/1024, my_cycles);

    int *my_results = malloc(sizeof(int) * num_threads);

    // Allocate regions of length (num_pages * PAGE_SIZE))
    char *my_arr = (char*)allocate_userfault(&my_ufd, my_arr_size);
    if (!my_arr) {
        fprintf(stderr, "failed to allocate userfault\n");
        return;
    }

    // Create array to be compared against
    // Allocating on heap to avoid stack overflows on large arrays
    char *my_cmp_bytes =  (char *) malloc(my_arr_size);

    pthread_t thread_id[num_threads];
    ThreadArgs thread_args[num_threads];

    int my_split = my_num_pages / num_threads;

    int c = 0;
    int i = 0;

    while (c < my_cycles) {
      // populate the array
      for(i=0; i < num_threads; i++) {
          thread_args[i].num_pages = my_num_pages;
          thread_args[i].split = my_split;
          thread_args[i].arr = my_arr;
          thread_args[i].cmp_bytes = my_cmp_bytes;
          thread_args[i].threadindex = i;
          pthread_create( &thread_id[i], NULL, write_arr, &thread_args[i]);
      }
      for(i=0; i < num_threads; i++) {
          pthread_join( thread_id[i], NULL);
      }

      // compare against cmp_bytes
      for(i=0; i < num_threads; i++) {
          thread_args[i].num_pages = my_num_pages;
          thread_args[i].split = my_split;
          thread_args[i].arr = my_arr;
          thread_args[i].cmp_bytes = my_cmp_bytes;
          thread_args[i].threadindex = i;
          thread_args[i].results = my_results;
          pthread_create( &thread_id[i], NULL, verify_arr, &thread_args[i]);
      }
      for(i=0; i < num_threads; i++) {
          pthread_join( thread_id[i], NULL);
          if (my_results[i] < 0) {
              fprintf(stderr, "%s: thread id %d failed fault_test\n", __func__, i);
              ret = -1;
          }
      }

      c++;
    }

    pthread_barrier_wait(&finish_barrier);

    // if we cleanup too early, we may get EINVAL when monitor tries evicting
    // since this thread is woken up on ZERO or COPY and eviction happens
    // asynchronously
    printf("TID %d: test is done, sleeping for 5 seconds to let all threads finish\n",
           syscall(SYS_gettid));
    sleep(5);

    // Cleanup
    int rc = disable_ufd_area(my_ufd, (void *)my_arr, my_arr_size);
    if (rc < 0)
        fprintf(stderr, "%s: failed to disable ufd area\n", __func__);
    close(ufd);
    free(cmp_bytes);
    free(results);

    return;

}

void *small_fault_test (void *args) {
    int *argPtr = args;
    int threadindex = *argPtr;

    results[threadindex] = fault_test(1, 1, 1);
}

int allocate_test(int num_ufds) {
    int ufd[num_ufds];
    pthread_t thread_id[num_ufds];
    int thread_args[num_ufds];
    int ret = 0;

    results = malloc(sizeof(int) * num_ufds);

    int i;
    for (i = 0; i < num_ufds; i++) {
        thread_args[i] = i;
        pthread_create( &thread_id[i], NULL, small_fault_test, &thread_args[i]);
    }

    for (i = 0; i < num_ufds; i++) {
        pthread_join( thread_id[i], NULL);
        if (results[i] < 0) {
            fprintf(stderr, "%s: thread id %d failed fault_test\n", __func__, i);
            ret = -1;
        }
    }

    free(results);
    return ret;
}

int start_threaded_fault_test(int num_threads, int num_pages, int cycles) {
    pthread_t thread_id[num_threads];
    ThreadArgs thread_args[num_threads];
    int i;
    int ret = 0;

    for(i=0; i < num_threads; i++) {
        thread_args[i].num_pages = num_pages;
        thread_args[i].cycles = cycles;
        thread_args[i].num_threads = num_threads;
        pthread_create( &thread_id[i], NULL, threaded_fault_test, &thread_args[i]);
    }
    for(i=0; i < num_threads; i++) {
        pthread_join( thread_id[i], NULL);
    }

    return ret;
}

int main (int argc, char *argv[]) {

    int test = 0;
    int ret = -1;

    if (argc > 1) {
        test = atoi(argv[1]);
        if (test == 1) {
            num_allocations = 100;

            pthread_barrier_init(&finish_barrier, NULL, num_allocations);
            ret = allocate_test(num_allocations);
            pthread_barrier_destroy(&finish_barrier);
        }
        else if (test == 2) {
            num_allocations = 1;

            pthread_barrier_init(&finish_barrier, NULL, num_allocations);
            ret = fault_test(1, 1, 1);
            pthread_barrier_destroy(&finish_barrier);
        }
        else if (test == 3) {
            num_allocations = 1;

            pthread_barrier_init(&finish_barrier, NULL, num_allocations);
            ret = fault_test(1000, 1000, 100);
            pthread_barrier_destroy(&finish_barrier);
        }
        else if (test == 4) {
            num_allocations = 1;

            pthread_barrier_init(&finish_barrier, NULL, num_allocations);
            ret = fault_test(4096, 4096, 1);
            pthread_barrier_destroy(&finish_barrier);
        }
        else if (test == 5) {
            num_allocations = 1;

            pthread_barrier_init(&finish_barrier, NULL, num_allocations);
            ret = start_threaded_fault_test(num_allocations, 4096, 1);
            pthread_barrier_destroy(&finish_barrier);
        }
        else if (test == 6) {
            num_allocations = 20;

            pthread_barrier_init(&finish_barrier, NULL, num_allocations);
            ret = start_threaded_fault_test(num_allocations, 1000, 5);
            pthread_barrier_destroy(&finish_barrier);
        }
        else if (test == 7) {
            // fault test, cache size 2048, region size 4096, cycles 5, reduce lru to 1
            num_allocations = 1;

            pthread_barrier_init(&finish_barrier, NULL, num_allocations);
            ret = fault_test(4096, 4096, 5);
            pthread_barrier_destroy(&finish_barrier);
        }
        else {
            print_usage();
            ret = -1;
        }
    }
    else {
        print_usage();
        ret = -1;
    }

    printf("test_cases returned %d\n", ret);
    return ret;

}
