/*
 * Demo 06: Fork-Join Safe (Structural Ordering Without Locks)
 *
 * Scenario:
 * 1. Main thread initializes shared input array 'input_data' and zeroes 'output_data'.
 * 2. Main thread forks worker threads (pthread_create).
 * 3. Workers read 'input_data' and write their partial results to 'output_data'.
 * 4. Main thread joins all workers (pthread_join).
 * 5. Main thread reads 'output_data' to compute total sum.
 *
 * Key Concept (Showcase vs Lockset False Positives):
 * - ZERO mutex locks are used.
 * - Pure Lockset (Eraser) flags this as a DATA RACE because candidate lockset is empty.
 * - Thread Sentinel's Hybrid Detector recognizes that fork (pthread_create) and
 *   join (pthread_join) establish causal structural happens-before ordering (ST clock).
 *
 * Expected Verdict:
 * - Pure Lockset: FALSE POSITIVE (flags race)
 * - Hybrid Detector: SAFE (0 races)
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "sentinel.h"

#define NUM_WORKERS 4

static int input_data[NUM_WORKERS];
static int output_data[NUM_WORKERS];

static void *worker(void *arg) {
    long id = (long)arg;
    
    /* Worker reads input prepared by main before pthread_create */
    int val = TS_READ(input_data[id]);
    
    /* Worker computes and stores result */
    TS_WRITE(output_data[id], val * 2);
    
    return NULL;
}

int main(void) {
    pthread_t workers[NUM_WORKERS];
    
    printf("Starting fork_join_safe with %d workers...\n", NUM_WORKERS);
    
    /* Step 1: Main initializes input data and zeroes the output slots. The workers'
     * later lock-free writes to output_data are ordered only by fork, which is what
     * Eraser cannot see (a main-thread write then a worker write, no common lock). */
    for (int i = 0; i < NUM_WORKERS; i++) {
        TS_WRITE(input_data[i], (i + 1) * 10);
        TS_WRITE(output_data[i], 0);
    }
    
    /* Step 2: Main forks workers */
    for (long i = 0; i < NUM_WORKERS; i++) {
        if (pthread_create(&workers[i], NULL, worker, (void *)i) != 0) {
            perror("pthread_create");
            return 1;
        }
    }
    
    /* Step 3: Main joins all workers */
    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_join(workers[i], NULL);
    }
    
    /* Step 4: Main reads outputs (guaranteed safe by pthread_join) */
    int total_sum = 0;
    for (int i = 0; i < NUM_WORKERS; i++) {
        total_sum += TS_READ(output_data[i]);
    }
    
    printf("Computed total sum = %d (expected 200)\n", total_sum);
    if (total_sum == 200) {
        printf("Result: CORRECT (fork-join structural ordering guaranteed correctness)\n");
    } else {
        printf("Result: INCORRECT\n");
    }
    
    return 0;
}
