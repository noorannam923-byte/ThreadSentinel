/*
 * Demo 01: Counter Race
 *
 * Scenario: 4 threads concurrently increment a shared counter 500,000 times each.
 * Expected total: 2,000,000 increments.
 *
 * Bug: No synchronization protects 'counter'. Interleaved read-modify-write operations
 * cause lost updates (data race).
 *
 * Expected Verdict: CONFIRMED RACE on 'counter'
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "sentinel.h"

#define NUM_THREADS 4
#define ITERATIONS  500000

static long counter = 0;

static void *worker(void *arg) {
    (void)arg;
    for (int i = 0; i < ITERATIONS; i++) {
        TS_UPDATE(counter, += 1);
    }
    return NULL;
}

int main(void) {
    pthread_t threads[NUM_THREADS];
    long expected = (long)NUM_THREADS * ITERATIONS;
    
    printf("Starting counter_race with %d threads, %d iterations each...\n",
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
    if (counter != expected) {
        printf("Result: DATA RACE DETECTED (lost %ld updates!)\n", expected - counter);
    } else {
        printf("Result: COUNTER MATCHES EXPECTED (race did not manifest in lost updates)\n");
    }
    
    return 0;
}
