/*
 * Demo 07: Lucky Lock Order (Happenstance Ordering Through Unrelated Lock)
 *
 * Scenario:
 * - Thread 1 writes to shared variable 'x' without holding any lock protecting 'x'.
 * - Thread 1 then locks and unlocks an UNRELATED mutex 'm'.
 * - Thread 2 subsequently locks and unlocks the same unrelated mutex 'm'.
 * - Thread 2 then writes to 'x' without holding any lock protecting 'x'.
 *
 * Key Concept (Showcase vs Pure Happens-Before False Negatives):
 * - In this particular execution schedule, T1 released 'm' before T2 acquired 'm'.
 * - Pure Happens-Before (HB) transfers causality through 'm', making it appear
 *   that T1's write "happened before" T2's write. Pure HB misses this bug!
 * - However, 'm' does not protect 'x', and this schedule ordering was purely accidental.
 *   On a different run, T2 could acquire 'm' before T1, producing a visible data race.
 * - Thread Sentinel's Structural Clock (ST) does NOT advance on mutex operations.
 *   Because 'hb_ordered' is true but 'st_ordered' is false and there is no common lock,
 *   Sentinel flags this as a POTENTIAL RACE!
 *
 * Expected Verdict:
 * - Pure HB: MISSED / SAFE (False Negative!)
 * - Pure Lockset: CONFIRMED
 * - Hybrid Detector: POTENTIAL RACE (Medium Severity)
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include "sentinel.h"

static int x = 0;
static pthread_mutex_t unrelated_mutex = PTHREAD_MUTEX_INITIALIZER;

static void *thread1(void *arg) {
    (void)arg;
    
    /* T1 writes to x with NO lock */
    TS_WRITE(x, 42);
    
    /* T1 touches an unrelated mutex */
    pthread_mutex_lock(&unrelated_mutex);
    pthread_mutex_unlock(&unrelated_mutex);
    
    return NULL;
}

static void *thread2(void *arg) {
    (void)arg;
    
    /* Ensure T1 runs its unlock first so lucky ordering occurs */
    usleep(10000); /* 10ms */
    
    /* T2 touches the unrelated mutex */
    pthread_mutex_lock(&unrelated_mutex);
    pthread_mutex_unlock(&unrelated_mutex);
    
    /* T2 writes to x with NO lock */
    TS_WRITE(x, 99);
    
    return NULL;
}

int main(void) {
    pthread_t t1, t2;
    
    printf("Starting lucky_lock_order demo...\n");
    
    if (pthread_create(&t1, NULL, thread1, NULL) != 0) {
        perror("pthread_create t1");
        return 1;
    }
    
    if (pthread_create(&t2, NULL, thread2, NULL) != 0) {
        perror("pthread_create t2");
        return 1;
    }
    
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    
    printf("Final x = %d (schedule-dependent, expected either 42 or 99)\n", x);
    printf("Result: SUCCESS (hybrid detector analyzed happenstance lock ordering)\n");
    return 0;
}
