/*
 * Demo 10: Mixed Locks (Inconsistent Mutual Exclusion)
 *
 * Scenario:
 * - Shared variable 'x' is accessed concurrently by two threads.
 * - Thread 1 protects 'x' using Mutex A.
 * - Thread 2 protects 'x' using Mutex B.
 *
 * Bug: Even though both threads diligently acquire a mutex, they do NOT share a COMMON lock!
 * Their candidate lockset intersection {A} ∩ {B} = ∅ (empty set).
 *
 * Expected Verdict: CONFIRMED RACE on 'x'
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "sentinel.h"

#define ITERATIONS 200000

static long x = 0;
static pthread_mutex_t lock_a = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t lock_b = PTHREAD_MUTEX_INITIALIZER;

static void *worker_a(void *arg) {
    (void)arg;
    for (int i = 0; i < ITERATIONS; i++) {
        pthread_mutex_lock(&lock_a);
        TS_UPDATE(x, += 1);
        pthread_mutex_unlock(&lock_a);
    }
    return NULL;
}

static void *worker_b(void *arg) {
    (void)arg;
    for (int i = 0; i < ITERATIONS; i++) {
        pthread_mutex_lock(&lock_b);
        TS_UPDATE(x, += 1);
        pthread_mutex_unlock(&lock_b);
    }
    return NULL;
}

int main(void) {
    pthread_t t1, t2;
    long expected = (long)ITERATIONS * 2;
    
    printf("Starting mixed_locks demo with %d iterations per thread...\n", ITERATIONS);
    
    if (pthread_create(&t1, NULL, worker_a, NULL) != 0 ||
        pthread_create(&t2, NULL, worker_b, NULL) != 0) {
        perror("pthread_create");
        return 1;
    }
    
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    
    printf("Final x = %ld (expected %ld)\n", x, expected);
    if (x != expected) {
        printf("Result: DATA RACE DETECTED (lost %ld updates)\n", expected - x);
    } else {
        printf("Result: x matches expected\n");
    }
    
    return 0;
}
