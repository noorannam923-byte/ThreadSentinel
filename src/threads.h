#ifndef THREADS_H
#define THREADS_H

#include "sentinel_internal.h"
#include "vector_clock.h"
#include <pthread.h>
#include <stdbool.h>

typedef struct {
    pthread_t handle;
    uint32_t tid;
    bool active;
    bool joined;
    vector_clock_t hb_clock;
    vector_clock_t st_clock;
    uint64_t locks_held; /* Bitmask of locks currently held by this thread */
} ts_thread_info_t;

extern __thread uint32_t g_ts_current_tid;
extern __thread bool g_ts_tid_initialized;

void ts_threads_init(void);
void ts_threads_shutdown(void);
uint32_t ts_get_tid(void);
ts_thread_info_t *ts_get_thread_info(uint32_t tid);
ts_thread_info_t *ts_current_thread_info(void);

#endif /* THREADS_H */
