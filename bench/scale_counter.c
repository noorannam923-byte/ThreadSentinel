/*
 * Scalability workload: N threads, each doing a fixed amount of work (weak scaling).
 *
 * Usage: scale_counter <threads> [iterations_per_thread]
 *
 * Each iteration writes a thread-private slot (uncontended, exercises shadow memory)
 * and every 8th iteration increments a shared counter under a mutex (exercises the
 * lock wrappers and vector clock joins). The program is race-free, so detector cost
 * is measured without report generation dominating.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "sentinel.h"

#define MAX_THREADS 64
#define SLOT_STRIDE 16 /* keep private slots on separate cache lines */

static long shared_counter = 0;
static long private_slots[MAX_THREADS * SLOT_STRIDE];
static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;
static long g_iterations = 20000;

static void *worker(void *arg) {
    long id = (long)arg;
    long *slot = &private_slots[id * SLOT_STRIDE];
    for (long i = 0; i < g_iterations; i++) {
        TS_UPDATE(*slot, += i);
        if ((i & 7) == 0) {
            pthread_mutex_lock(&counter_mutex);
            TS_UPDATE(shared_counter, += 1);
            pthread_mutex_unlock(&counter_mutex);
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    int nthreads = (argc > 1) ? atoi(argv[1]) : 4;
    if (argc > 2) g_iterations = atol(argv[2]);
    if (nthreads < 1 || nthreads > MAX_THREADS - 1) {
        fprintf(stderr, "threads must be in 1..%d\n", MAX_THREADS - 1);
        return 1;
    }

    pthread_t threads[MAX_THREADS];
    for (long i = 0; i < nthreads; i++) {
        if (pthread_create(&threads[i], NULL, worker, (void *)i) != 0) {
            perror("pthread_create");
            return 1;
        }
    }
    for (int i = 0; i < nthreads; i++) {
        pthread_join(threads[i], NULL);
    }

    long expected = (long)nthreads * ((g_iterations + 7) / 8);
    printf("threads=%d iterations=%ld shared_counter=%ld (expected %ld)\n",
           nthreads, g_iterations, shared_counter, expected);
    return shared_counter == expected ? 0 : 2;
}
