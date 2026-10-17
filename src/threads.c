#include "threads.h"
#include "sentinel_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__thread uint32_t g_ts_current_tid = 0;
__thread bool g_ts_tid_initialized = false;

static ts_thread_info_t g_thread_table[TS_MAX_THREADS];
static uint32_t g_thread_count = 1;

typedef struct {
    void *(*user_routine)(void *);
    void *user_arg;
    uint32_t child_tid;
    vector_clock_t parent_hb;
    vector_clock_t parent_st;
} ts_trampoline_args_t;

void ts_threads_init(void) {
    memset(g_thread_table, 0, sizeof(g_thread_table));
    g_ts_current_tid = 0;
    g_ts_tid_initialized = true;
    
    g_thread_table[0].tid = 0;
    g_thread_table[0].handle = pthread_self();
    g_thread_table[0].active = true;
    g_thread_table[0].joined = false;
    g_thread_table[0].locks_held = 0;
    
    vc_init(&g_thread_table[0].hb_clock);
    vc_init(&g_thread_table[0].st_clock);
    /* Rule: Thread start C_t[t] = 1, rest 0 */
    g_thread_table[0].hb_clock.clock[0] = 1;
    g_thread_table[0].st_clock.clock[0] = 1;
    
    g_thread_count = 1;
}

void ts_threads_shutdown(void) {
    /* No dynamically allocated memory in static table */
}

uint32_t ts_get_tid(void) {
    if (!g_ts_tid_initialized) {
        /* Default to 0 (main thread) if called before explicit registration */
        return 0;
    }
    return g_ts_current_tid;
}

ts_thread_info_t *ts_get_thread_info(uint32_t tid) {
    if (tid >= TS_MAX_THREADS) return NULL;
    return &g_thread_table[tid];
}

ts_thread_info_t *ts_current_thread_info(void) {
    return ts_get_thread_info(ts_get_tid());
}

static void *ts_thread_trampoline(void *arg) {
    ts_trampoline_args_t *targs = (ts_trampoline_args_t *)arg;
    uint32_t tid = targs->child_tid;
    void *(*user_routine)(void *) = targs->user_routine;
    void *user_arg = targs->user_arg;
    
    /* Set thread-local variables */
    g_ts_current_tid = tid;
    g_ts_tid_initialized = true;
    
    ts_lock_runtime();
    g_thread_table[tid].handle = pthread_self();
    g_thread_table[tid].active = true;
    g_thread_table[tid].joined = false;
    g_thread_table[tid].locks_held = 0;
    
    /* Child clock starts as copy of parent clock, with C_u[u]++ */
    vc_copy(&g_thread_table[tid].hb_clock, &targs->parent_hb);
    vc_copy(&g_thread_table[tid].st_clock, &targs->parent_st);
    vc_increment(&g_thread_table[tid].hb_clock, tid);
    vc_increment(&g_thread_table[tid].st_clock, tid);
    
    free(targs);
    ts_unlock_runtime();
    
    /* Execute user routine */
    void *retval = user_routine(user_arg);
    
    /* Mark thread inactive on exit */
    ts_lock_runtime();
    g_thread_table[tid].active = false;
    ts_unlock_runtime();
    
    return retval;
}

int ts_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                      void *(*start_routine)(void *), void *arg) {
    ts_lock_runtime();
    
    /* Find an available tid */
    uint32_t child_tid = 0;
    for (uint32_t i = 1; i < TS_MAX_THREADS; i++) {
        if (!g_thread_table[i].active && !g_thread_table[i].handle) {
            child_tid = i;
            break;
        }
    }
    
    if (child_tid == 0) {
        fprintf(stderr, "[ThreadSentinel ERROR] Exceeded TS_MAX_THREADS (%d)\n", TS_MAX_THREADS);
        ts_unlock_runtime();
        abort();
    }
    
    uint32_t parent_tid = ts_get_tid();
    ts_thread_info_t *parent_info = &g_thread_table[parent_tid];
    
    /* Prepare trampoline arguments with snapshot of parent clock */
    ts_trampoline_args_t *targs = (ts_trampoline_args_t *)malloc(sizeof(ts_trampoline_args_t));
    if (!targs) {
        ts_unlock_runtime();
        return -1;
    }
    targs->user_routine = start_routine;
    targs->user_arg = arg;
    targs->child_tid = child_tid;
    vc_copy(&targs->parent_hb, &parent_info->hb_clock);
    vc_copy(&targs->parent_st, &parent_info->st_clock);
    
    /* Rule: create(t -> u): C_t[t]++ */
    vc_increment(&parent_info->hb_clock, parent_tid);
    vc_increment(&parent_info->st_clock, parent_tid);
    
    /* Reserve slot */
    g_thread_table[child_tid].tid = child_tid;
    g_thread_table[child_tid].active = true;
    if (child_tid >= g_thread_count) {
        g_thread_count = child_tid + 1;
    }
    
    ts_unlock_runtime();
    
    int ret = pthread_create(thread, attr, ts_thread_trampoline, targs);
    if (ret != 0) {
        ts_lock_runtime();
        g_thread_table[child_tid].active = false;
        ts_unlock_runtime();
        free(targs);
    } else {
        ts_lock_runtime();
        g_thread_table[child_tid].handle = *thread;
        ts_unlock_runtime();
    }
    
    return ret;
}

int ts_pthread_join(pthread_t thread, void **retval) {
    int ret = pthread_join(thread, retval);
    if (ret != 0) {
        return ret;
    }
    
    ts_lock_runtime();
    uint32_t current_tid = ts_get_tid();
    ts_thread_info_t *cur_info = &g_thread_table[current_tid];
    
    /* Find child tid from thread handle */
    int target_tid = -1;
    for (uint32_t i = 1; i < TS_MAX_THREADS; i++) {
        if (pthread_equal(g_thread_table[i].handle, thread)) {
            target_tid = (int)i;
            break;
        }
    }
    
    if (target_tid >= 0) {
        ts_thread_info_t *target_info = &g_thread_table[target_tid];
        /* Rule: join(t waits for u): C_t = max(C_t, C_u_final); C_t[t]++ */
        vc_join(&cur_info->hb_clock, &target_info->hb_clock);
        vc_join(&cur_info->st_clock, &target_info->st_clock);
        vc_increment(&cur_info->hb_clock, current_tid);
        vc_increment(&cur_info->st_clock, current_tid);
        
        target_info->joined = true;
    }
    
    ts_unlock_runtime();
    return 0;
}
