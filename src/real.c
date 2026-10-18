/* Lazily resolved pointers to libc's pthread functions (LD_PRELOAD build only). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define TS_REAL_IMPL
#include "real.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

static void *resolve(const char *name, const char *version) {
    void *fn = NULL;
    /* condvar functions have an old and a new symbol version; ask for the current one */
    if (version) fn = dlvsym(RTLD_NEXT, name, version);
    if (!fn) fn = dlsym(RTLD_NEXT, name);
    if (!fn) {
        fprintf(stderr, "[ThreadSentinel ERROR] preload: cannot resolve %s\n", name);
        abort();
    }
    return fn;
}

/*
 * Each pointer starts at a stub that resolves the real function, stores it and
 * forwards the call, so the pointers work even if they are used before any
 * constructor (ours or the program's) has run.
 */
#define REAL_FN(ret, name, version, params, args)                     \
    static ret resolve_##name params;                                  \
    ret (*ts_real_##name) params = resolve_##name;                     \
    static ret resolve_##name params {                                 \
        *(void **)&ts_real_##name = resolve(#name, version);           \
        return ts_real_##name args;                                    \
    }

REAL_FN(int, pthread_create, NULL,
        (pthread_t *t, const pthread_attr_t *a, void *(*fn)(void *), void *arg), (t, a, fn, arg))
REAL_FN(int, pthread_join, NULL, (pthread_t t, void **ret), (t, ret))
REAL_FN(int, pthread_mutex_lock, NULL, (pthread_mutex_t *m), (m))
REAL_FN(int, pthread_mutex_trylock, NULL, (pthread_mutex_t *m), (m))
REAL_FN(int, pthread_mutex_unlock, NULL, (pthread_mutex_t *m), (m))
REAL_FN(int, pthread_cond_wait, "GLIBC_2.3.2", (pthread_cond_t *c, pthread_mutex_t *m), (c, m))
REAL_FN(int, pthread_cond_signal, "GLIBC_2.3.2", (pthread_cond_t *c), (c))
REAL_FN(int, pthread_cond_broadcast, "GLIBC_2.3.2", (pthread_cond_t *c), (c))
REAL_FN(int, pthread_barrier_wait, NULL, (pthread_barrier_t *b), (b))
