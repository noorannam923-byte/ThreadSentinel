/*
 * Demo 09: Read-Only Shared (Safe Concurrent Readers)
 *
 * Scenario:
 * - Shared configuration table initialized once by main thread.
 * - Multiple worker threads concurrently read from the table without locks.
 *
 * Key Concept:
 * - Read-only sharing of initialized immutable data is completely thread-safe.
 * - The Eraser state machine enters the SHARED state. Because no writes ever occur
 *   after multiple readers access the variable, no race is reported.
 *
 * Expected Verdict: SAFE (0 races)
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "sentinel.h"

#define TABLE_SIZE 100
#define NUM_READERS 4

static int config_table[TABLE_SIZE];

static void *reader_worker(void *arg) {
    long id = (long)arg;
    long sum = 0;
    
    for (int iter = 0; iter < 1000; iter++) {
        for (int i = 0; i < TABLE_SIZE; i++) {
            sum += TS_READ(config_table[i]);
        }
    }
    
    (void)id;
    return (void *)sum;
}

int main(void) {
    pthread_t readers[NUM_READERS];
    
    printf("Starting read_only_shared demo with %d readers...\n", NUM_READERS);
    
    /* Initialization phase */
    for (int i = 0; i < TABLE_SIZE; i++) {
        TS_WRITE(config_table[i], (i + 1) * 10);
    }
    
    for (long i = 0; i < NUM_READERS; i++) {
        if (pthread_create(&readers[i], NULL, reader_worker, (void *)i) != 0) {
            perror("pthread_create");
            return 1;
        }
    }
    
    long expected_per_reader = 0;
    for (int i = 0; i < TABLE_SIZE; i++) {
        expected_per_reader += (i + 1) * 10;
    }
    expected_per_reader *= 1000;
    
    bool all_correct = true;
    for (int i = 0; i < NUM_READERS; i++) {
        void *res;
        pthread_join(readers[i], &res);
        if ((long)res != expected_per_reader) {
            all_correct = false;
        }
    }
    
    if (all_correct) {
        printf("Result: CORRECT (concurrent read-only sharing is completely safe)\n");
    } else {
        printf("Result: FAILED\n");
    }
    
    return 0;
}
