/*
 * Demo 13: Lock-Order Inversion (Potential Deadlock)
 *
 * Scenario:
 * - Two accounts, each with its own mutex. Thread 1 transfers A -> B and locks
 *   lock_a then lock_b. Thread 2 transfers B -> A and locks lock_b then lock_a.
 * - Thread 2 starts 10 ms later, so on this run thread 1 has released both locks
 *   before thread 2 begins and the program finishes normally.
 *
 * Key Concept:
 * - There is no data race (every balance access holds both locks), and this
 *   schedule did not deadlock. With the opposite timing each thread could hold its
 *   first lock and wait forever for the other's.
 * - Thread Sentinel records "lock_b acquired while holding lock_a" and the reverse
 *   in a lock-order graph; the cycle lock_a -> lock_b -> lock_a is reported as a
 *   POTENTIAL DEADLOCK from this single successful run.
 *
 * Expected: 0 races, 1 potential deadlock (also detectable on the uninstrumented
 * build via LD_PRELOAD=libsentinel_preload.so).
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include "sentinel.h"

static long balance_a = 1000;
static long balance_b = 1000;
static pthread_mutex_t lock_a = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t lock_b = PTHREAD_MUTEX_INITIALIZER;

static void *transfer_a_to_b(void *arg) {
    (void)arg;
    pthread_mutex_lock(&lock_a);
    pthread_mutex_lock(&lock_b);
    TS_UPDATE(balance_a, -= 100);
    TS_UPDATE(balance_b, += 100);
    pthread_mutex_unlock(&lock_b);
    pthread_mutex_unlock(&lock_a);
    return NULL;
}

static void *transfer_b_to_a(void *arg) {
    (void)arg;
    usleep(10000); /* 10 ms: let thread 1 finish so this run does not actually deadlock */
    pthread_mutex_lock(&lock_b);
    pthread_mutex_lock(&lock_a); /* opposite order: lock_b -> lock_a */
    TS_UPDATE(balance_b, -= 50);
    TS_UPDATE(balance_a, += 50);
    pthread_mutex_unlock(&lock_a);
    pthread_mutex_unlock(&lock_b);
    return NULL;
}

int main(void) {
    pthread_t t1, t2;

    printf("Starting lock_order_inversion demo...\n");

    if (pthread_create(&t1, NULL, transfer_a_to_b, NULL) != 0 ||
        pthread_create(&t2, NULL, transfer_b_to_a, NULL) != 0) {
        perror("pthread_create");
        return 1;
    }
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    long total = balance_a + balance_b;
    printf("Final balances: A = %ld, B = %ld, total = %ld (expected 2000)\n",
           balance_a, balance_b, total);
    printf("Result: %s\n", total == 2000 ? "Total preserved (no deadlock on this schedule)"
                                         : "TOTAL CORRUPTED");
    return 0;
}
