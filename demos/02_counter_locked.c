/*
 * Demo 02: Counter Locked (Correct Synchronization)
 *
 * Scenario: 4 threads concurrently increment a shared counter 500,000 times each.
 * All accesses are protected by a single shared pthread_mutex.
 *
 * Expected Verdict: SAFE (0 races)
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "sentinel.h"

#define NUM_THREADS 4
#define ITERATIONS  500000

static long counter = 0;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static void *worker(void *arg) {
    (void)arg;
    for (int i = 0; i < ITERATIONS; i++) {
        pthread_mutex_lock(&lock);
        TS_UPDATE(counter, += 1);
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

int main(void) {
    pthread_t threads[NUM_THREADS];
    long expected = (long)NUM_THREADS * ITERATIONS;
    
    printf("Starting counter_locked with %d threads, %d iterations each...\n",
           NUM_THREADS, ITERATIONS);
           
    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, worker, NULL) != 0) {
            perror("pthread_create");
            return 1;
        }
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("Final counter = %ld (expected %ld)\n", counter, expected);
    if (counter == expected) {
        printf("Result: CORRECT (mutex properly prevented data races)\n");
    } else {
        printf("Result: UNEXPECTED CORRUPTION (lost %ld updates)\n", expected - counter);
    }
    
    return 0;
}
