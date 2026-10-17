/*
 * Demo 03: Bank Transfer (Asymmetric / Incomplete Locking)
 *
 * Scenario:
 * - Two bank accounts: Account A (balance 10,000) and Account B (balance 10,000).
 * - Total money must always equal 20,000 (preservation of total balance invariant).
 * - Thread 1 transfers money from A to B: locks lock_a, but omits lock_b when crediting B.
 * - Thread 2 transfers money from B to A: locks lock_b, but omits lock_a when crediting A.
 *
 * Bug: Incomplete locking violates the common lockset rule and happens-before causality,
 * resulting in data races on account balances and lost money.
 *
 * Expected Verdict: CONFIRMED RACE
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "sentinel.h"

#define NUM_TRANSFERS 100000

static long account_a_bal = 10000;
static long account_b_bal = 10000;

static pthread_mutex_t lock_a = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t lock_b = PTHREAD_MUTEX_INITIALIZER;

static void *transfer_a_to_b(void *arg) {
    (void)arg;
    for (int i = 0; i < NUM_TRANSFERS; i++) {
        pthread_mutex_lock(&lock_a);
        TS_UPDATE(account_a_bal, -= 10);
        /* Bug: Updates account_b_bal without acquiring lock_b */
        TS_UPDATE(account_b_bal, += 10);
        pthread_mutex_unlock(&lock_a);
    }
    return NULL;
}

static void *transfer_b_to_a(void *arg) {
    (void)arg;
    for (int i = 0; i < NUM_TRANSFERS; i++) {
        pthread_mutex_lock(&lock_b);
        TS_UPDATE(account_b_bal, -= 10);
        /* Bug: Updates account_a_bal without acquiring lock_a */
        TS_UPDATE(account_a_bal, += 10);
        pthread_mutex_unlock(&lock_b);
    }
    return NULL;
}

int main(void) {
    pthread_t t1, t2;
    long expected_total = 20000;
    
    printf("Starting bank_transfer demo (%d transfers each way)...\n", NUM_TRANSFERS);
    
    if (pthread_create(&t1, NULL, transfer_a_to_b, NULL) != 0 ||
        pthread_create(&t2, NULL, transfer_b_to_a, NULL) != 0) {
        perror("pthread_create");
        return 1;
    }
    
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    
    long total = account_a_bal + account_b_bal;
    printf("Final Balances: A = %ld, B = %ld (Total = %ld, Expected = %ld)\n",
           account_a_bal, account_b_bal, total, expected_total);
           
    if (total != expected_total) {
        printf("Result: MONEY CORRUPTED (discrepancy = %ld)\n", expected_total - total);
    } else {
        printf("Result: Total matches expected\n");
    }
    
    return 0;
}
