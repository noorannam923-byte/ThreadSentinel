/*
 * Demo 08: Condvar Handoff Safe (Condition Variable Structural Ordering)
 *
 * Scenario:
 * - Thread 1 (Producer) prepares shared data 'payload = 12345' and sets 'ready = 1',
 *   then signals Condition Variable 'cond'.
 * - Thread 2 (Consumer) waits on 'cond' until 'ready == 1'.
 * - Once awakened, Thread 2 reads 'payload'.
 *
 * Key Concept:
 * - Condvar signal and wait establish causal structural happens-before ordering (ST clock).
 * - Therefore, access to 'payload' is causally ordered and completely race-free.
 *
 * Expected Verdict: SAFE (0 races)
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "sentinel.h"

static int payload = 0;
static int ready = 0;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

static void *sender(void *arg) {
    (void)arg;
    
    pthread_mutex_lock(&mtx);
    TS_WRITE(payload, 12345);
    TS_WRITE(ready, 1);
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mtx);
    
    return NULL;
}

static void *receiver(void *arg) {
    (void)arg;
    int received_val = 0;
    
    pthread_mutex_lock(&mtx);
    while (!TS_READ(ready)) {
        pthread_cond_wait(&cond, &mtx);
    }
    received_val = TS_READ(payload);
    pthread_mutex_unlock(&mtx);
    
    printf("Receiver received payload = %d (expected 12345)\n", received_val);
    return NULL;
}

int main(void) {
    pthread_t t_send, t_recv;
    
    printf("Starting condvar_handoff_safe demo...\n");
    
    if (pthread_create(&t_recv, NULL, receiver, NULL) != 0 ||
        pthread_create(&t_send, NULL, sender, NULL) != 0) {
        perror("pthread_create");
        return 1;
    }
    
    pthread_join(t_send, NULL);
    pthread_join(t_recv, NULL);
    
    printf("Result: CORRECT (condvar handoff synchronized successfully)\n");
    return 0;
}
