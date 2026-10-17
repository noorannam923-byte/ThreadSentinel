/*
 * Demo 12: Linked List Race (Concurrent Push to Unsynchronized Head)
 *
 * Scenario:
 * - Multiple worker threads concurrently prepend nodes to a shared linked list.
 * - Each thread allocates a new node and executes:
 *     new_node->next = TS_READ(list_head);
 *     TS_WRITE(list_head, new_node);
 *
 * Bug: Unsynchronized concurrent modification of 'list_head' causes lost nodes
 * and memory leaks.
 *
 * Expected Verdict: CONFIRMED RACE on 'list_head'
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "sentinel.h"

#define NUM_THREADS 4
#define NODES_PER_THREAD 10000

typedef struct node {
    int val;
    struct node *next;
} node_t;

static node_t *list_head = NULL;

static void *list_worker(void *arg) {
    long id = (long)arg;
    for (int i = 0; i < NODES_PER_THREAD; i++) {
        node_t *new_node = (node_t *)malloc(sizeof(node_t));
        new_node->val = (int)(id * 100000 + i);
        
        /* Bug: Unsynchronized read and write to list_head */
        new_node->next = TS_READ(list_head);
        TS_WRITE(list_head, new_node);
    }
    return NULL;
}

int main(void) {
    pthread_t threads[NUM_THREADS];
    int expected_nodes = NUM_THREADS * NODES_PER_THREAD;
    
    printf("Starting linked_list_race with %d threads (%d nodes each)...\n",
           NUM_THREADS, NODES_PER_THREAD);
           
    for (long i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, list_worker, (void *)i) != 0) {
            perror("pthread_create");
            return 1;
        }
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    /* Count total nodes reached from list_head */
    int count = 0;
    node_t *cur = list_head;
    while (cur) {
        count++;
        node_t *tmp = cur;
        cur = cur->next;
        TS_FREE(tmp, sizeof(node_t));
    }
    
    printf("Final list node count = %d (expected %d)\n", count, expected_nodes);
    if (count != expected_nodes) {
        printf("Result: DATA RACE DETECTED (lost %d nodes!)\n", expected_nodes - count);
    } else {
        printf("Result: All nodes preserved\n");
    }
    
    return 0;
}
