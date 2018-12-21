// A C program to show implementation of LRU cache
#include <stdio.h>
#include <stdlib.h>
#include <dbg.h>

// A Queue Node (Queue is implemented using Doubly Linked List)
typedef struct QNode
{
    struct QNode *prev, *next;
    int64_t page_addr;  // the page number stored in this QNode
    uint8_t page_contents[PAGE_SIZE];  // a 4k array with the data of the page
    bool page_present;
    int ufd;
} QNode;

// A Queue (A FIFO collection of Queue Nodes)
typedef struct Queue
{
    unsigned count;  // Number of filled frames
    unsigned numberOfFrames; // total number of frames
    QNode *front, *rear;
} Queue;

typedef struct HNode
{
    int64_t page_addr;
    QNode* qNode;
    struct HNode* next;
} HNode;

// A hash (Collection of pointers to Queue Nodes)
typedef struct Hash
{
    int capacity; // how many pages can be there
    HNode* *array; // an array of queue nodes
} Hash;

/* C cache settings. This has been replaced by LRUBuffer written in c++ */
#include "lrucache.h"
#define CACHE_SIZE 10
#define HASH_SIZE 10

Queue* cache_queue;
Hash* cache_hash;

// A utility function to create a new Queue Node. The queue Node
// will store the given 'pageNumber'
QNode* newQNode( int64_t page_addr, uint8_t* src, int ufd )
{
    log_trace_in("newQNode");
    // Allocate memory and assign 'pageNumber'
    QNode* temp = (QNode *)malloc( sizeof( QNode ) );
    temp->page_addr = page_addr;
    temp->ufd = ufd;
    if (src != NULL) {
      temp->page_present = true;
      memcpy( temp->page_contents, (void*)src, PAGE_SIZE );
    }
    else {
      temp->page_present = false;
    }

    // Initialize prev and next as NULL
    temp->prev = temp->next = NULL;

    log_cache_debug("cache: crated a new cache(QNode) for the virtual addr %p, src addr %p", (void *)(uintptr_t)page_addr, src);

    log_trace_out("newQNode");
    return temp;
}

// A utility function to create an empty Queue.
// The queue can have at most 'numberOfFrames' nodes
Queue* createQueue( int numberOfFrames )
{
    Queue* queue = (Queue *)malloc( sizeof( Queue ) );

    // The queue is empty
    queue->count = 0;
    queue->front = queue->rear = NULL;

    // Number of frames that can be stored in memory
    queue->numberOfFrames = numberOfFrames;

    return queue;
}

void printQueue()
{
    printf( "Queue count : %d\n", cache_queue->count );
    QNode* qn = cache_queue->front;
    int j=0;
    while( qn!=NULL )
    {
        printf("page %d, page addr : %p, page contents : ", j, (void *)(uintptr_t)qn->page_addr);
        int i=0;
        if (qn->page_present)
        {
          for( ; i<20; i++ )
              printf( "%c", (char) qn->page_contents[i] );
        }
        else
        {
           printf( "NOT PRESENT" );
        }
        printf( "\n");
        qn = qn->next;
        j++;
    }
}

void printHash()
{
    printf( "Hash capacity : %d\n", cache_hash->capacity );
    int i=0;
    for( ; i<cache_hash->capacity; i++ )
    {
        printf("[%d]", i );
        if( cache_hash->array[i]==NULL )
            printf( "empty" );
        else
        {
            HNode * cur = cache_hash->array[i];
            for( ; cur!=NULL; cur = cur->next )
	    {
                printf("page addr : %p, page contents : ", (void *)(uintptr_t)cur->qNode->page_addr);
                int j=0;
                if (cur->qNode->page_present)
                {
                  for( ; j<20; j++ )
                      printf( "%c", (char) cur->qNode->page_contents[j] );
                }
                else
                {
                  printf( "NOT PRESENT" );
                }
                printf( " " );
            }
            printf( "\n");
        }
    }
}

// A utility function to create an empty Hash of given capacity
Hash* createHash( int capacity )
{
    // Allocate memory for hash
    Hash* hash = (Hash *) malloc( sizeof( Hash ) );
    hash->capacity = capacity;

    // Create an array of pointers for refering queue nodes
    hash->array = (HNode**) malloc( hash->capacity * sizeof( HNode* ) );

    // Initialize all hash entries as empty
    int i;
    for( i = 0; i < hash->capacity; ++i )
    {
        hash->array[i] = NULL;
    }
    return hash;
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

QNode * findQNodeFromHash( Hash * hash, int64_t page_addr )
{
    int idx = jenkins_hash( (char *) &page_addr, 8 )%hash->capacity;
    if( hash->array[idx]==NULL )
	return NULL;
    else
    {
        HNode * cur = hash->array[idx];
        for( ; cur!=NULL; cur = cur->next )
	    {
	        if( cur->page_addr == page_addr )
		        return cur->qNode;
        }
    	return NULL;
    }
}

void insertHash( Hash * hash, QNode * qnode, int64_t page_addr )
{
    log_trace_in("insertHash");
    int idx = jenkins_hash( (char *) &page_addr, 8 )%hash->capacity;
    if( hash->array[idx]==NULL )
    {
        hash->array[idx] = (HNode*) malloc( sizeof( struct HNode ) );
        hash->array[idx]->page_addr = page_addr;
        hash->array[idx]->qNode = qnode;
        hash->array[idx]->next = NULL;
    }
    else
    {
        HNode * cur = hash->array[idx];
        HNode * prev = NULL;
        for( ; cur!=NULL; cur = cur->next )
        {
            if( cur->page_addr == page_addr )
                return; // already exist
            prev = cur;
        }
        prev->next = malloc( sizeof( struct HNode ) );
        cur = prev->next;
        cur->page_addr = page_addr;
        cur->qNode = qnode;
        cur->next = NULL;
    }
    log_cache_debug("cache: inserted into the hash for the virtual addr %p. idx is %d", (void *)(uintptr_t)page_addr, idx);
    log_trace_out("insertHash");
}

void removeHash( Hash * hash, int64_t page_addr )
{
    log_trace_in("removeHash");

    int idx = jenkins_hash( (char *) &page_addr, 8 )%hash->capacity;
    log_cache_debug("cache: removing a hash item for the virtual addr %p. idx is %d", (void *)(uintptr_t)page_addr, idx);
    if( hash->array[idx]==NULL )
    {
        return;
    }
    else
    {
        HNode * cur = hash->array[idx];
        HNode * prev = NULL;
        for( ; cur!=NULL; cur = cur->next )
        {
            if( cur->page_addr == page_addr )
            {
                if( prev==NULL )
                    hash->array[idx] = cur->next;
                else
                    prev->next = cur->next;
                free( cur );
                return;
            }
            prev = cur;
        }
    }
    log_trace_out("removeHash");
}

// A function to check if there is slot available in memory
int AreAllFramesFull( Queue* queue )
{
    return queue->count == queue->numberOfFrames;
}

// A utility function to check if queue is empty
int isQueueEmpty( Queue* queue )
{
    return queue->rear == NULL;
}

/*
 * A utility function to delete a frame from queue
 *
 * Note: the QNode that was isolated from the queue list is not free'd.
 *       A pointer to it is returned for the caller to free
 */

QNode* deQueue( Queue* queue )
{
    log_trace_in("deQueue");

    QNode *temp;

    if( isQueueEmpty( queue ) )
        return NULL;

    /* the page to dequeue */
    temp = queue->rear;

    /*
     * try writing the page back to externram. return on failure, since the page
     * isn't actually removed
     */
    if (evict_to_externram(temp->ufd, (void*)(uintptr_t)temp->page_addr) < 0) {
        log_err("deQueue: evict_to_externram");
        return NULL;
    }

    // If this is the only node in list, then change front
    if (queue->front == queue->rear)
        queue->front = NULL;

    // Change rear and remove the previous rear
    queue->rear = queue->rear->prev;

    if (queue->rear)
        queue->rear->next = NULL;

    // decrement the number of full frames by 1
    queue->count--;
    log_trace_out("deQueue");

    /* return pointer to the isolated QNode */
    return temp;
}

// A function to add a page with given 'pageNumber' to both queue
// and hash
void Enqueue( Queue* queue, Hash* hash, int64_t page_addr, uint8_t* src, int ufd )
{
    log_trace_in("Enqueue");
    // If all frames are full, remove the page at the rear
    if ( AreAllFramesFull ( queue ) )
    {
        QNode* removed;
        removed = deQueue( queue );
        if (removed == NULL) {
            log_err("Enqueue: failed to deQueue to make room");
            return;
        }

        // remove page from hash
        removeHash( hash, removed->page_addr );

        /* Now the QNode can be free'd */
        free(removed);
    }

    // Create a new node with given page number,
    // And add the new node to the front of queue
    QNode* temp = newQNode( page_addr, src, ufd );
    temp->next = queue->front;

    // If queue is empty, change both front and rear pointers
    if ( isQueueEmpty( queue ) ) {
        queue->rear = queue->front = temp;
    }
    else  // Else change the front
    {
        queue->front->prev = temp;
        queue->front = temp;
    }

    // Add page entry to hash also
    insertHash( hash, temp, page_addr );

    // increment number of full frames
    queue->count++;
    log_trace_out("Enqueue");
}

// This function is called when a page with given 'pageNumber' is referenced
// from cache (or memory). There are two cases:
// 1. Frame is not there in memory, we bring it in memory and add to the front
//    of queue
// 2. Frame is there in memory, we move the frame to front of queue
void ReferencePage( Queue* queue, Hash* hash, uint64_t page_addr, uint8_t * src, int ufd )
{
    log_trace_in("ReferencePage");
    QNode* reqPage = findQNodeFromHash( hash, page_addr );

    // the page is not in cache, bring it
    if ( reqPage == NULL )
        Enqueue( queue, hash, page_addr, src, ufd );

    // page is there and not at front, change pointer
    else if (reqPage != queue->front)
    {
        // Unlink rquested page from its current location
        // in queue.
        reqPage->prev->next = reqPage->next;
        if (reqPage->next)
            reqPage->next->prev = reqPage->prev;

        // If the requested page is rear, then change rear
        // as this node will be moved to front
        if (reqPage == queue->rear)
        {
            queue->rear = reqPage->prev;
            queue->rear->next = NULL;
        }

        // Put the requested page before current front
        reqPage->next = queue->front;
        reqPage->prev = NULL;

        // Change prev of current front
        reqPage->next->prev = reqPage;

        // Change front to the requested page
        queue->front = reqPage;
    }

    log_trace_out("ReferencePage");
}


