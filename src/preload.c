/*
 * LD_PRELOAD shim: libsentinel_preload.so
 *
 *   LD_PRELOAD=./libsentinel_preload.so ./unmodified_program
 *
 * Interposes the pthread thread/mutex/condvar/barrier functions of a program that
 * was NOT compiled against sentinel.h and routes them through the runtime's
 * wrappers. Thread clocks, locksets and the lock-order graph are maintained, so
 * potential deadlocks are reported; memory accesses are invisible without the
 * TS_* macros, so data races are not.
 *
 * Do not combine with a binary already linked against libsentinel.a.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define TS_INTERNAL 1   /* keep sentinel.h from renaming the functions defined below */
#include "sentinel.h"
#include <stdio.h>

volatile int ts_preload_active = 0;

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg) {
    ts_preload_active = 1;
    return ts_pthread_create(thread, attr, start_routine, arg);
}

int pthread_join(pthread_t thread, void **retval) {
    ts_preload_active = 1;
    return ts_pthread_join(thread, retval);
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    ts_preload_active = 1;
    /* No source location without the macros: report the caller's code address */
    char name[32], where[32];
    snprintf(name, sizeof(name), "mutex@%p", (void *)mutex);
    snprintf(where, sizeof(where), "pc %p", __builtin_return_address(0));
    return ts_mutex_lock_at(mutex, name, where, -1);
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    ts_preload_active = 1;
    return ts_mutex_trylock(mutex);
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    ts_preload_active = 1;
    return ts_mutex_unlock(mutex);
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    ts_preload_active = 1;
    return ts_cond_wait(cond, mutex);
}

int pthread_cond_signal(pthread_cond_t *cond) {
    ts_preload_active = 1;
    return ts_cond_signal(cond);
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
    ts_preload_active = 1;
    return ts_cond_broadcast(cond);
}

int pthread_barrier_wait(pthread_barrier_t *barrier) {
    ts_preload_active = 1;
    return ts_barrier_wait(barrier);
}
