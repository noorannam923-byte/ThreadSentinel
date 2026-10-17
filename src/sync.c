#include "sync.h"
#include "sentinel_internal.h"
#include "threads.h"
#include "lockset.h"
#include "vector_clock.h"
#include "trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SYNC_OBJECTS 256

typedef struct {
    pthread_mutex_t *mutex;
    int lock_id;
    vector_clock_t hb_clock; /* L_m: clock of the last thread to unlock */
} ts_mutex_record_t;

typedef struct {
    pthread_cond_t *cond;
    vector_clock_t hb_clock; /* S_c: signal clock */
    vector_clock_t st_clock;
} ts_cond_record_t;

typedef struct {
    pthread_barrier_t *barrier;
    vector_clock_t arrival_hb;
    vector_clock_t arrival_st;
    uint32_t generation;
    int arrived_count;
    int departed_count;
} ts_barrier_record_t;

static ts_mutex_record_t g_mutex_table[MAX_SYNC_OBJECTS];
static int g_mutex_count = 0;

static ts_cond_record_t g_cond_table[MAX_SYNC_OBJECTS];
static int g_cond_count = 0;

static ts_barrier_record_t g_barrier_table[MAX_SYNC_OBJECTS];
static int g_barrier_count = 0;

void ts_sync_init(void) {
    memset(g_mutex_table, 0, sizeof(g_mutex_table));
    g_mutex_count = 0;
    memset(g_cond_table, 0, sizeof(g_cond_table));
    g_cond_count = 0;
    memset(g_barrier_table, 0, sizeof(g_barrier_table));
    g_barrier_count = 0;
}

void ts_sync_shutdown(void) {
}

static ts_mutex_record_t *get_or_create_mutex(pthread_mutex_t *mutex) {
    for (int i = 0; i < g_mutex_count; i++) {
        if (g_mutex_table[i].mutex == mutex) {
            return &g_mutex_table[i];
        }
    }
    if (g_mutex_count >= MAX_SYNC_OBJECTS) {
        fprintf(stderr, "[ThreadSentinel ERROR] Exceeded MAX_SYNC_OBJECTS mutexes\n");
        abort();
    }
    ts_mutex_record_t *rec = &g_mutex_table[g_mutex_count++];
    rec->mutex = mutex;
    rec->lock_id = lockset_assign_id(mutex);
    vc_init(&rec->hb_clock);
    return rec;
}

static ts_cond_record_t *get_or_create_cond(pthread_cond_t *cond) {
    for (int i = 0; i < g_cond_count; i++) {
        if (g_cond_table[i].cond == cond) {
            return &g_cond_table[i];
        }
    }
    if (g_cond_count >= MAX_SYNC_OBJECTS) {
        fprintf(stderr, "[ThreadSentinel ERROR] Exceeded MAX_SYNC_OBJECTS condvars\n");
        abort();
    }
    ts_cond_record_t *rec = &g_cond_table[g_cond_count++];
    rec->cond = cond;
    vc_init(&rec->hb_clock);
    vc_init(&rec->st_clock);
    return rec;
}

static ts_barrier_record_t *get_or_create_barrier(pthread_barrier_t *barrier) {
    for (int i = 0; i < g_barrier_count; i++) {
        if (g_barrier_table[i].barrier == barrier) {
            return &g_barrier_table[i];
        }
    }
    if (g_barrier_count >= MAX_SYNC_OBJECTS) {
        fprintf(stderr, "[ThreadSentinel ERROR] Exceeded MAX_SYNC_OBJECTS barriers\n");
        abort();
    }
    ts_barrier_record_t *rec = &g_barrier_table[g_barrier_count++];
    rec->barrier = barrier;
    vc_init(&rec->arrival_hb);
    vc_init(&rec->arrival_st);
    rec->generation = 0;
    rec->arrived_count = 0;
    rec->departed_count = 0;
    return rec;
}

int ts_mutex_lock(pthread_mutex_t *mutex) {
    /* Call the real POSIX mutex lock */
    int ret = pthread_mutex_lock(mutex);
    if (ret != 0) return ret;
    
    ts_lock_runtime();
    uint32_t tid = ts_get_tid();
    ts_thread_info_t *tinfo = ts_get_thread_info(tid);
    ts_mutex_record_t *rec = get_or_create_mutex(mutex);
    
    /* Rule: lock(m) by t: HB only: HB_t = max(HB_t, L_m) */
    vc_join(&tinfo->hb_clock, &rec->hb_clock);
    lockset_add_held(&tinfo->locks_held, rec->lock_id);
    
    trace_record_event(tid, "lock", mutex, rec->lock_id, NULL, NULL, 0, &tinfo->hb_clock, &tinfo->st_clock);
    
    ts_unlock_runtime();
    return 0;
}

int ts_mutex_trylock(pthread_mutex_t *mutex) {
    int ret = pthread_mutex_trylock(mutex);
    if (ret != 0) return ret;
    
    ts_lock_runtime();
    uint32_t tid = ts_get_tid();
    ts_thread_info_t *tinfo = ts_get_thread_info(tid);
    ts_mutex_record_t *rec = get_or_create_mutex(mutex);
    
    /* Successful trylock: update HB clock and locks held */
    vc_join(&tinfo->hb_clock, &rec->hb_clock);
    lockset_add_held(&tinfo->locks_held, rec->lock_id);
    
    trace_record_event(tid, "lock", mutex, rec->lock_id, NULL, NULL, 0, &tinfo->hb_clock, &tinfo->st_clock);
    
    ts_unlock_runtime();
    return 0;
}

int ts_mutex_unlock(pthread_mutex_t *mutex) {
    ts_lock_runtime();
    uint32_t tid = ts_get_tid();
    ts_thread_info_t *tinfo = ts_get_thread_info(tid);
    ts_mutex_record_t *rec = get_or_create_mutex(mutex);
    
    /* Rule: unlock(m) by t: HB only: L_m = HB_t; HB_t[t]++ */
    vc_copy(&rec->hb_clock, &tinfo->hb_clock);
    vc_increment(&tinfo->hb_clock, tid);
    lockset_remove_held(&tinfo->locks_held, rec->lock_id);
    
    trace_record_event(tid, "unlock", mutex, rec->lock_id, NULL, NULL, 0, &tinfo->hb_clock, &tinfo->st_clock);
    
    ts_unlock_runtime();
    return pthread_mutex_unlock(mutex);
}

int ts_cond_signal(pthread_cond_t *cond) {
    ts_lock_runtime();
    uint32_t tid = ts_get_tid();
    ts_thread_info_t *tinfo = ts_get_thread_info(tid);
    ts_cond_record_t *rec = get_or_create_cond(cond);
    
    /* Rule: cond_signal(c) by t: S_c = max(S_c, C_t); C_t[t]++ */
    vc_join(&rec->hb_clock, &tinfo->hb_clock);
    vc_join(&rec->st_clock, &tinfo->st_clock);
    vc_increment(&tinfo->hb_clock, tid);
    vc_increment(&tinfo->st_clock, tid);
    
    trace_record_event(tid, "cond_signal", cond, -1, NULL, NULL, 0, &tinfo->hb_clock, &tinfo->st_clock);
    
    ts_unlock_runtime();
    return pthread_cond_signal(cond);
}

int ts_cond_broadcast(pthread_cond_t *cond) {
    ts_lock_runtime();
    uint32_t tid = ts_get_tid();
    ts_thread_info_t *tinfo = ts_get_thread_info(tid);
    ts_cond_record_t *rec = get_or_create_cond(cond);
    
    /* Rule: cond_broadcast(c) by t: S_c = max(S_c, C_t); C_t[t]++ */
    vc_join(&rec->hb_clock, &tinfo->hb_clock);
    vc_join(&rec->st_clock, &tinfo->st_clock);
    vc_increment(&tinfo->hb_clock, tid);
    vc_increment(&tinfo->st_clock, tid);
    
    trace_record_event(tid, "cond_signal", cond, -1, NULL, NULL, 0, &tinfo->hb_clock, &tinfo->st_clock);
    
    ts_unlock_runtime();
    return pthread_cond_broadcast(cond);
}

int ts_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    /* Before waiting, the associated mutex is released */
    ts_lock_runtime();
    uint32_t tid = ts_get_tid();
    ts_thread_info_t *tinfo = ts_get_thread_info(tid);
    ts_mutex_record_t *mrec = get_or_create_mutex(mutex);
    
    /* Unlock rule for mutex */
    vc_copy(&mrec->hb_clock, &tinfo->hb_clock);
    vc_increment(&tinfo->hb_clock, tid);
    lockset_remove_held(&tinfo->locks_held, mrec->lock_id);
    
    trace_record_event(tid, "cond_wait_start", cond, mrec->lock_id, NULL, NULL, 0, &tinfo->hb_clock, &tinfo->st_clock);
    ts_unlock_runtime();
    
    int ret = pthread_cond_wait(cond, mutex);
    
    /* After wait returns, condvar handoff is received and mutex is relocked */
    ts_lock_runtime();
    ts_cond_record_t *crec = get_or_create_cond(cond);
    
    /* Rule: cond_wait(c) returns in t: C_t = max(C_t, S_c) (both HB and ST) */
    vc_join(&tinfo->hb_clock, &crec->hb_clock);
    vc_join(&tinfo->st_clock, &crec->st_clock);
    
    /* Plus mutex lock rule: HB_t = max(HB_t, L_m) */
    vc_join(&tinfo->hb_clock, &mrec->hb_clock);
    lockset_add_held(&tinfo->locks_held, mrec->lock_id);
    
    trace_record_event(tid, "cond_wait", cond, mrec->lock_id, NULL, NULL, 0, &tinfo->hb_clock, &tinfo->st_clock);
    
    ts_unlock_runtime();
    return ret;
}

int ts_barrier_wait(pthread_barrier_t *barrier) {
    ts_lock_runtime();
    uint32_t tid = ts_get_tid();
    ts_thread_info_t *tinfo = ts_get_thread_info(tid);
    ts_barrier_record_t *rec = get_or_create_barrier(barrier);
    
    /* Barrier arrival: accumulate clock */
    vc_join(&rec->arrival_hb, &tinfo->hb_clock);
    vc_join(&rec->arrival_st, &tinfo->st_clock);
    rec->arrived_count++;
    
    trace_record_event(tid, "barrier_arrive", barrier, -1, NULL, NULL, 0, &tinfo->hb_clock, &tinfo->st_clock);
    ts_unlock_runtime();
    
    int ret = pthread_barrier_wait(barrier);
    
    /* Barrier departure: all participants have arrived, merge accumulated clock */
    ts_lock_runtime();
    vc_join(&tinfo->hb_clock, &rec->arrival_hb);
    vc_join(&tinfo->st_clock, &rec->arrival_st);
    vc_increment(&tinfo->hb_clock, tid);
    vc_increment(&tinfo->st_clock, tid);
    
    rec->departed_count++;
    trace_record_event(tid, "barrier", barrier, -1, NULL, NULL, 0, &tinfo->hb_clock, &tinfo->st_clock);
    
    ts_unlock_runtime();
    return ret;
}
