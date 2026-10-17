/*
 * Demo 05: Producer-Consumer Bug (Unsynchronized Buffer Count Read)
 *
 * Scenario:
 * - Producer thread generates items into a shared buffer, updating 'count' under a mutex.
 * - Consumer thread checks if buffer is empty by reading 'count' OUTSIDE the mutex lock!
 *
 * Bug: Reading shared variable 'count' without acquiring the buffer mutex causes
 * a concurrent read-write data race with the producer.
 *
 * Expected Verdict: CONFIRMED RACE on 'count'
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include "sentinel.h"

#define ITEMS_COUNT 10000

static int buffer[128];
static int count = 0;
static int head = 0;
static int tail = 0;
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;

static void *producer(void *arg) {
    (void)arg;
    for (int i = 0; i < ITEMS_COUNT; i++) {
        pthread_mutex_lock(&queue_lock);
        buffer[tail] = i + 1;
        tail = (tail + 1) % 128;
        TS_UPDATE(count, += 1);
        pthread_mutex_unlock(&queue_lock);
    }
    return NULL;
}

static void *consumer(void *arg) {
    (void)arg;
    int items_consumed = 0;
    while (items_consumed < ITEMS_COUNT) {
        /* Bug: Reading 'count' without holding queue_lock */
        int cur_count = TS_READ(count);
        if (cur_count > 0) {
            pthread_mutex_lock(&queue_lock);
            if (count > 0) {
                int val = buffer[head];
                head = (head + 1) % 128;
                TS_UPDATE(count, -= 1);
                (void)val;
                items_consumed++;
            }
            pthread_mutex_unlock(&queue_lock);
        }
    }
    return NULL;
}

int main(void) {
    pthread_t prod, cons;
    
    printf("Starting producer_consumer_bug demo (%d items)...\n", ITEMS_COUNT);
    
    if (pthread_create(&prod, NULL, producer, NULL) != 0 ||
        pthread_create(&cons, NULL, consumer, NULL) != 0) {
        perror("pthread_create");
        return 1;
    }
    
    pthread_join(prod, NULL);
    pthread_join(cons, NULL);
    
    printf("Finished producer-consumer. Final count = %d (expected 0)\n", count);
    if (count == 0) {
        printf("Result: Consumed all items\n");
    } else {
        printf("Result: Buffer count mismatch!\n");
    }
    
    return 0;
}
