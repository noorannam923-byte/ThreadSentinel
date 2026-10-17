/*
 * Demo 04: Check-Then-Act Race (Unsynchronized Lazy Initialization)
 *
 * Scenario:
 * - Multiple threads attempt to initialize a shared resource lazily.
 * - Pattern: if (!initialized) { initialize(); initialized = 1; }
 *
 * Bug: Between the check (TS_READ) and the state update (TS_WRITE), other threads
 * concurrently observe initialized == 0 and enter the critical section, causing
 * multiple initializations and conflicting accesses.
 *
 * Expected Verdict: CONFIRMED RACE on 'initialized' and 'init_count'
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include "sentinel.h"

#define NUM_THREADS 8

static int initialized = 0;
static int init_count = 0;

static void *init_worker(void *arg) {
    (void)arg;
    
    /* Bug: Unsynchronized check-then-act */
    if (!TS_READ(initialized)) {
        usleep(500); /* Small window widening the race */
        TS_UPDATE(init_count, += 1);
        TS_WRITE(initialized, 1);
    }
    
    return NULL;
}

int main(void) {
    pthread_t threads[NUM_THREADS];
    
    printf("Starting check_then_act demo with %d threads...\n", NUM_THREADS);
    
    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, init_worker, NULL) != 0) {
            perror("pthread_create");
            return 1;
        }
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("Final state: initialized = %d, init_count = %d (expected 1)\n",
           initialized, init_count);
           
    if (init_count > 1) {
        printf("Result: MULTIPLE INITIALIZATIONS DETECTED (init_count = %d)\n", init_count);
    } else {
        printf("Result: Initialized exactly once\n");
    }
    
    return 0;
}
