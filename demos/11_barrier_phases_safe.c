/*
 * Demo 11: Barrier Phases Safe (Multi-Phase Synchronization via Barrier)
 *
 * Scenario:
 * - 4 worker threads execute in coordinated computation phases separated by a barrier.
 * - Phase 1: Each thread i computes and writes to array[i].
 * - Synchronization: All threads rendezvous at 'pthread_barrier_wait'.
 * - Phase 2: Each thread i reads array[(i + 1) % 4] (written by a different thread in Phase 1)
 *   and then clears it.
 *
 * Expected Verdict (Pure Lockset): FALSE POSITIVE - two threads write each slot with no
 * common lock; only the barrier orders them.
 *
 * Key Concept:
 * - POSIX barriers synchronize all participant threads simultaneously.
 * - Thread Sentinel merges vector clocks of all arriving threads and propagates
 *   the joint clock to all departing threads.
 * - This establishes causal structural happens-before ordering across phases without mutexes!
 *
 * Expected Verdict: SAFE (0 races)
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "sentinel.h"

#define NUM_THREADS 4

static int phase_data[NUM_THREADS];
static int next_phase_data[NUM_THREADS];
static pthread_barrier_t barrier;

static void *phase_worker(void *arg) {
    long id = (long)arg;
    
    /* Phase 1: Write own slot */
    TS_WRITE(phase_data[id], (int)(id + 1) * 10);
    
    /* Rendezvous: all Phase 1 writes must complete before any Phase 2 read */
    pthread_barrier_wait(&barrier);
    
    /* Phase 2: Read neighboring slot written in Phase 1 */
    int neighbor_id = (id + 1) % NUM_THREADS;
    int neighbor_val = TS_READ(phase_data[neighbor_id]);
    /* Consume the slot: a lock-free write by a different thread than its Phase 1
     * writer, ordered only by the barrier (Eraser flags it, the hybrid detector does not) */
    TS_WRITE(phase_data[neighbor_id], 0);

    TS_WRITE(next_phase_data[id], neighbor_val + 5);
    
    return NULL;
}

int main(void) {
    pthread_t threads[NUM_THREADS];
    
    printf("Starting barrier_phases_safe demo with %d threads...\n", NUM_THREADS);
    
    if (pthread_barrier_init(&barrier, NULL, NUM_THREADS) != 0) {
        perror("pthread_barrier_init");
        return 1;
    }
    
    for (long i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, phase_worker, (void *)i) != 0) {
            perror("pthread_create");
            return 1;
        }
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    pthread_barrier_destroy(&barrier);
    
    bool all_valid = true;
    for (int i = 0; i < NUM_THREADS; i++) {
        int expected = ((i + 1) % NUM_THREADS + 1) * 10 + 5;
        if (next_phase_data[i] != expected) {
            all_valid = false;
        }
    }
    
    if (all_valid) {
        printf("Result: CORRECT (barrier successfully separated phases)\n");
    } else {
        printf("Result: FAILED\n");
    }
    
    return 0;
}
